#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "chitra.h"
#include "chitra_rhi.h"
#include "chitra_table.h"
#include "chitra_batch.h"
#include <stdio.h>
#include <string.h>

#include "shaders/gen/chitra_shape.glsl.h"
#include "shaders/gen/chitra_text.glsl.h"
#include "shaders/gen/chitra_texture.glsl.h"

#define CHITRA_RHI_INIT_VERTICES 256
#define CHITRA_RHI_INIT_INDICES  384
#define CHITRA_RHI_MAX_TEXTURES  64

typedef struct {
    sg_image img;
    sg_view  view;
    bool     active;
    bool     is_rgba;
} RHI_Texture;

static struct {
    sg_pipeline  pip[CHITRA_PIPELINE_COUNT];
    sg_bindings  bind[CHITRA_PIPELINE_COUNT];

    /* Direct Transient GPU Buffers (Zero Staging Overhead) */
    sg_buffer    vbuf[CHITRA_PIPELINE_COUNT];
    size_t       vbuf_capacity[CHITRA_PIPELINE_COUNT]; /* in bytes */

    sg_buffer    ibuf;
    size_t       ibuf_capacity;                        /* in bytes */

    sg_sampler   sampler;

    int          current_width;
    int          current_height;

    /* Compile-Time Static Texture Registry (Zero Heap Allocation, 768B in BSS) */
    RHI_Texture  textures[CHITRA_RHI_MAX_TEXTURES];
} s_rhi = {0};

/* =========================================================================
 * Subsystem Initialization
 * ========================================================================= */

static void rhi_init(const Chitra_Config* config) {
    int ub_size   = (config && config->uniform_buffer_size > 0) ? config->uniform_buffer_size : (128 * 1024);
    int max_buf   = (config && config->max_buffers > 0) ? config->max_buffers : 32;

    /* Compact resource pools and 128KB uniform ring for minimal memory footprint */
    sg_desc desc = {
        .environment         = sglue_environment(),
        .logger.func         = slog_func,
        .uniform_buffer_size = ub_size,
        .buffer_pool_size    = max_buf,
        .image_pool_size     = CHITRA_RHI_MAX_TEXTURES,
        .view_pool_size      = CHITRA_RHI_MAX_TEXTURES,
        .pipeline_pool_size  = 8,
        .shader_pool_size    = 4,
        .sampler_pool_size   = 4,
    };
    sg_setup(&desc);

    /* 1. Dynamic 32-Bit Index Buffer (Direct write_transient, zero staging buffer) */
    s_rhi.ibuf_capacity = CHITRA_RHI_INIT_INDICES * sizeof(uint32_t);
    s_rhi.ibuf = sg_make_buffer(&(sg_buffer_desc){
        .size = s_rhi.ibuf_capacity,
        .usage = { .index_buffer = true, .write_transient = true },
        .label = "chitra_ibuf_32"
    });

    /* 2. Shared Linear Texture Sampler */
    s_rhi.sampler = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
        .label = "chitra_sampler"
    });

    /* 3. Shape Pipeline (40-byte vertex, SDF analytical shader) */
    sg_pipeline_desc shape_pip_desc = {
        .layout = {
            .attrs = {
                [ATTR_shape_pos]           = { .format = SG_VERTEXFORMAT_FLOAT2 },
                [ATTR_shape_uv]            = { .format = SG_VERTEXFORMAT_FLOAT2 },
                [ATTR_shape_size]          = { .format = SG_VERTEXFORMAT_FLOAT2 },
                [ATTR_shape_radius_border] = { .format = SG_VERTEXFORMAT_FLOAT2 },
                [ATTR_shape_fill_color]    = { .format = SG_VERTEXFORMAT_UBYTE4N },
                [ATTR_shape_border_color]  = { .format = SG_VERTEXFORMAT_UBYTE4N }
            }
        },
        .shader = sg_make_shader(shape_shader_desc(sg_query_backend())),
        .index_type = SG_INDEXTYPE_UINT32,
        .colors[0].blend = {
            .enabled = true,
            .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
            .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .src_factor_alpha = SG_BLENDFACTOR_ONE,
            .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA
        }
    };
    s_rhi.pip[CHITRA_PIPELINE_SHAPE] = sg_make_pipeline(&shape_pip_desc);

    /* 4. Text Pipeline (20-byte vertex, alpha modulation) */
    sg_pipeline_desc text_pip_desc = {
        .layout = {
            .attrs = {
                [ATTR_text_pos]   = { .format = SG_VERTEXFORMAT_FLOAT2 },
                [ATTR_text_uv]    = { .format = SG_VERTEXFORMAT_FLOAT2 },
                [ATTR_text_color] = { .format = SG_VERTEXFORMAT_UBYTE4N }
            }
        },
        .shader = sg_make_shader(text_shader_desc(sg_query_backend())),
        .index_type = SG_INDEXTYPE_UINT32,
        .colors[0].blend = shape_pip_desc.colors[0].blend
    };
    s_rhi.pip[CHITRA_PIPELINE_TEXT] = sg_make_pipeline(&text_pip_desc);

    /* 5. Texture Pipeline (20-byte vertex, RGBA color modulation) */
    sg_pipeline_desc texture_pip_desc = {
        .layout = {
            .attrs = {
                [ATTR_texture_pos]   = { .format = SG_VERTEXFORMAT_FLOAT2 },
                [ATTR_texture_uv]    = { .format = SG_VERTEXFORMAT_FLOAT2 },
                [ATTR_texture_color] = { .format = SG_VERTEXFORMAT_UBYTE4N }
            }
        },
        .shader = sg_make_shader(texture_shader_desc(sg_query_backend())),
        .index_type = SG_INDEXTYPE_UINT32,
        .colors[0].blend = shape_pip_desc.colors[0].blend
    };
    s_rhi.pip[CHITRA_PIPELINE_TEXTURE] = sg_make_pipeline(&texture_pip_desc);

    /* 5. Direct Transient Vertex Buffers per Pipeline (Zero staging buffers, starts sub-100KB) */
    size_t strides[CHITRA_PIPELINE_COUNT] = {
        sizeof(Chitra_ShapeVertex),
        sizeof(Chitra_TextVertex),
        sizeof(Chitra_TextVertex)
    };

    for (int i = 0; i < CHITRA_PIPELINE_COUNT; i++) {
        s_rhi.vbuf_capacity[i] = CHITRA_RHI_INIT_VERTICES * strides[i];
        s_rhi.vbuf[i] = sg_make_buffer(&(sg_buffer_desc){
            .size = s_rhi.vbuf_capacity[i],
            .usage = { .vertex_buffer = true, .write_transient = true },
            .label = "chitra_vbuf"
        });

        s_rhi.bind[i].vertex_buffers[0] = s_rhi.vbuf[i];
        s_rhi.bind[i].index_buffer      = s_rhi.ibuf;
        s_rhi.bind[i].samplers[0]       = s_rhi.sampler;
    }
}

/* =========================================================================
 * Frame Execution & Direct Zero-Overhead Data Streaming
 * ========================================================================= */

static void rhi_begin_frame(int width, int height) {
    s_rhi.current_width = width;
    s_rhi.current_height = height;

    sg_pass_action pass_action = {
        .colors[0] = { .load_action = SG_LOADACTION_CLEAR, .clear_value = { 0.1f, 0.1f, 0.1f, 1.0f } }
    };
    sg_begin_pass(&(sg_pass){ .action = pass_action, .swapchain = sglue_swapchain() });
}

static void rhi_upload_vertices(uint8_t pipeline, const void* data, size_t size) {
    if (pipeline >= CHITRA_PIPELINE_COUNT || !data || size == 0) return;

    /* Dynamic Growth: Reallocate GPU buffer if size exceeds current capacity */
    if (size > s_rhi.vbuf_capacity[pipeline]) {
        size_t new_cap = (size_t)(s_rhi.vbuf_capacity[pipeline] * 1.5f);
        if (new_cap < size) new_cap = size + 1024;

        sg_destroy_buffer(s_rhi.vbuf[pipeline]);

        s_rhi.vbuf[pipeline] = sg_make_buffer(&(sg_buffer_desc){
            .size = new_cap,
            .usage = { .vertex_buffer = true, .write_transient = true },
            .label = "chitra_vbuf"
        });

        s_rhi.vbuf_capacity[pipeline] = new_cap;
        s_rhi.bind[pipeline].vertex_buffers[0] = s_rhi.vbuf[pipeline];
    }

    /* Direct CPU-to-GPU Streaming: Zero intermediate staging buffer, zero copies */
    sg_write_buffer_transient(&(sg_write_buffer_desc){
        .dst.buffer = s_rhi.vbuf[pipeline],
        .src.data   = { .ptr = data, .size = size }
    });
}

static void rhi_upload_indices(const uint32_t* index_data, size_t size) {
    if (!index_data || size == 0) return;

    /* Dynamic Growth: Reallocate GPU buffer if size exceeds current capacity */
    if (size > s_rhi.ibuf_capacity) {
        size_t new_cap = (size_t)(s_rhi.ibuf_capacity * 1.5f);
        if (new_cap < size) new_cap = size + 1024;

        sg_destroy_buffer(s_rhi.ibuf);

        s_rhi.ibuf = sg_make_buffer(&(sg_buffer_desc){
            .size = new_cap,
            .usage = { .index_buffer = true, .write_transient = true },
            .label = "chitra_ibuf_32"
        });

        s_rhi.ibuf_capacity = new_cap;

        for (int i = 0; i < CHITRA_PIPELINE_COUNT; i++) {
            s_rhi.bind[i].index_buffer = s_rhi.ibuf;
        }
    }

    /* Direct CPU-to-GPU Streaming: Zero intermediate staging buffer, zero copies */
    sg_write_buffer_transient(&(sg_write_buffer_desc){
        .dst.buffer = s_rhi.ibuf,
        .src.data   = { .ptr = index_data, .size = size }
    });
}

static void rhi_set_scissor(int x, int y, int w, int h) {
    sg_apply_scissor_rect(x, y, w, h, true);
}

static void rhi_draw_batch(const Chitra_DrawBatch* batch) {
    if (!batch || batch->num_elements == 0) return;

    uint8_t pip = batch->pipeline;
    if (pip >= CHITRA_PIPELINE_COUNT) return;

    sg_apply_pipeline(s_rhi.pip[pip]);

    /* Bind dynamic texture view if texture pipeline */
    if (pip != CHITRA_PIPELINE_SHAPE) {
        if (batch->texture_id < CHITRA_RHI_MAX_TEXTURES && s_rhi.textures[batch->texture_id].active) {
            s_rhi.bind[pip].views[0] = s_rhi.textures[batch->texture_id].view;
        }
    }

    sg_apply_bindings(&s_rhi.bind[pip]);

    if (pip == CHITRA_PIPELINE_SHAPE) {
        vs_params_t shape_params = {
            .u_resolution = { (float)s_rhi.current_width, (float)s_rhi.current_height }
        };
        sg_apply_uniforms(UB_vs_params, &SG_RANGE(shape_params));
    } else if (pip == CHITRA_PIPELINE_TEXT) {
        text_vs_params_t text_params = {
            .u_resolution = { (float)s_rhi.current_width, (float)s_rhi.current_height }
        };
        sg_apply_uniforms(UB_text_vs_params, &SG_RANGE(text_params));
    } else if (pip == CHITRA_PIPELINE_TEXTURE) {
        texture_vs_params_t texture_params = {
            .u_resolution = { (float)s_rhi.current_width, (float)s_rhi.current_height }
        };
        sg_apply_uniforms(UB_texture_vs_params, &SG_RANGE(texture_params));
    }

    /* Dispatch 32-bit indexed draw call */
    sg_draw((int)batch->base_element, (int)batch->num_elements, 1);
}

static void rhi_end_frame(void) {
    sg_end_pass();
    sg_commit();
}

/* =========================================================================
 * Texture Atlas Management (Compile-Time Registry & On-Demand Upload)
 * ========================================================================= */

static uint16_t rhi_create_texture_internal(int width, int height, bool is_rgba) {
    int slot = -1;
    for (int i = 1; i < CHITRA_RHI_MAX_TEXTURES; i++) {
        if (!s_rhi.textures[i].active) { slot = i; break; }
    }
    if (slot == -1) return 0;

    sg_image img = sg_make_image(&(sg_image_desc){
        .width = width,
        .height = height,
        .pixel_format = is_rgba ? SG_PIXELFORMAT_RGBA8 : SG_PIXELFORMAT_R8,
        .usage = { .copy_dst = true },
        .label = is_rgba ? "chitra_texture_rgba" : "chitra_texture_r8"
    });

    sg_view view = sg_make_view(&(sg_view_desc){
        .texture = { .image = img },
        .label = "chitra_view"
    });

    s_rhi.textures[slot].img = img;
    s_rhi.textures[slot].view = view;
    s_rhi.textures[slot].active = true;
    s_rhi.textures[slot].is_rgba = is_rgba;

    return (uint16_t)slot;
}

static uint16_t rhi_create_texture(int width, int height) {
    return rhi_create_texture_internal(width, height, false);
}

static uint16_t rhi_create_texture_rgba(int width, int height) {
    return rhi_create_texture_internal(width, height, true);
}

static void rhi_update_texture_internal(uint16_t id, const void* data, int width, int height) {
    if (id == 0 || id >= CHITRA_RHI_MAX_TEXTURES || !s_rhi.textures[id].active) return;
    sg_image img = s_rhi.textures[id].img;
    size_t upload_size = (size_t)(width * height) * (s_rhi.textures[id].is_rgba ? 4 : 1);

    /* Transient staging buffer created strictly for this upload and destroyed immediately */
    sg_buffer staging = sg_make_buffer(&(sg_buffer_desc){
        .size = upload_size,
        .usage = { .staging_buffer = true, .write_transient = true, .copy_src = true },
        .label = "chitra_staging"
    });

    sg_write_buffer_transient(&(sg_write_buffer_desc){
        .src.data   = { .ptr = data, .size = upload_size },
        .dst.buffer = staging
    });

    sg_copy_buffer_to_image(&(sg_copy_buffer_to_image_desc){
        .src = { .buffer = staging },
        .dst = { .image = img },
        .size = { .width = width, .height = height, .num_slices = 1 }
    });

    /* Free upload buffer immediately: zero lingering staging memory */
    sg_destroy_buffer(staging);
}

static void rhi_update_texture(uint16_t id, const unsigned char* data, int width, int height) {
    rhi_update_texture_internal(id, data, width, height);
}

static void rhi_update_texture_rgba(uint16_t id, const void* data, int width, int height) {
    rhi_update_texture_internal(id, data, width, height);
}

static void rhi_destroy_texture(uint16_t id) {
    if (id == 0 || id >= CHITRA_RHI_MAX_TEXTURES || !s_rhi.textures[id].active) return;
    sg_destroy_view(s_rhi.textures[id].view);
    sg_destroy_image(s_rhi.textures[id].img);
    s_rhi.textures[id].active = false;
}

static void rhi_cleanup(void) {
    sg_shutdown();
}

/* =========================================================================
 * V-Table Export
 * ========================================================================= */

static const Chitra_RHI_API s_api = {
    .init                = rhi_init,
    .begin_frame         = rhi_begin_frame,
    .upload_vertices     = rhi_upload_vertices,
    .upload_indices      = rhi_upload_indices,
    .set_scissor         = rhi_set_scissor,
    .draw_batch          = rhi_draw_batch,
    .end_frame           = rhi_end_frame,
    .cleanup             = rhi_cleanup,
    .create_texture      = rhi_create_texture,
    .update_texture      = rhi_update_texture,
    .create_texture_rgba = rhi_create_texture_rgba,
    .update_texture_rgba = rhi_update_texture_rgba,
    .destroy_texture     = rhi_destroy_texture
};

const Chitra_RHI_API* chitra_rhi_get_api(void) {
    return &s_api;
}
