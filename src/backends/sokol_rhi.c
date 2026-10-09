
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "chitra_rhi.h"
#include "chitra_cmd.h"
#include <stdio.h>
#include <string.h>

#include "shaders/gen/chitra_shape.glsl.h"
#include "shaders/gen/chitra_text.glsl.h"

#define CHITRA_MAX_VERTICES (128 * 1024)
#define CHITRA_MAX_INDICES  (CHITRA_MAX_VERTICES * 6 / 4)

static struct {
    sg_pipeline shape_pip;
    sg_pipeline text_pip;
    
    sg_bindings shape_bind;
    sg_bindings text_bind;
    
    sg_buffer shape_staging;
    sg_buffer text_staging;
    sg_buffer index_staging;
    
    int current_width;
    int current_height;
} s_rhi = {0};

static void rhi_init(void) {
    sg_desc desc = {
        .environment = sglue_environment(),
        .logger.func = slog_func,
    };
    sg_setup(&desc);
    
    /* 1. Main Buffers (Copy Dst - Retained in VRAM) */
    s_rhi.shape_bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
        .size = CHITRA_MAX_VERTICES * sizeof(Chitra_ShapeVertex),
        .usage = { .vertex_buffer = true, .copy_dst = true },
        .label = "chitra_shape_vbuf"
    });
    
    s_rhi.text_bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
        .size = CHITRA_MAX_VERTICES * sizeof(Chitra_TextVertex),
        .usage = { .vertex_buffer = true, .copy_dst = true },
        .label = "chitra_text_vbuf"
    });
    
    sg_buffer ibuf = sg_make_buffer(&(sg_buffer_desc){
        .size = CHITRA_MAX_INDICES * sizeof(uint16_t),
        .usage = { .index_buffer = true, .copy_dst = true },
        .label = "chitra_ibuf"
    });
    s_rhi.shape_bind.index_buffer = ibuf;
    s_rhi.text_bind.index_buffer = ibuf;
    
    /* 2. Staging Buffers (Write Transient - CPU to GPU) */
    s_rhi.shape_staging = sg_make_buffer(&(sg_buffer_desc){
        .size = CHITRA_MAX_VERTICES * sizeof(Chitra_ShapeVertex),
        .usage = { .staging_buffer = true, .write_transient = true, .copy_src = true },
        .label = "chitra_shape_staging"
    });
    
    s_rhi.text_staging = sg_make_buffer(&(sg_buffer_desc){
        .size = CHITRA_MAX_VERTICES * sizeof(Chitra_TextVertex),
        .usage = { .staging_buffer = true, .write_transient = true, .copy_src = true },
        .label = "chitra_text_staging"
    });
    
    s_rhi.index_staging = sg_make_buffer(&(sg_buffer_desc){
        .size = CHITRA_MAX_INDICES * sizeof(uint16_t),
        .usage = { .staging_index_buffer = true, .write_transient = true, .copy_src = true },
        .label = "chitra_index_staging"
    });
    
    /* 3. Shape Pipeline */
    sg_pipeline_desc shape_pip_desc = {
        .layout = {
            .attrs = {
                [ATTR_shape_pos] = { .format = SG_VERTEXFORMAT_FLOAT2 },
                [ATTR_shape_uv] = { .format = SG_VERTEXFORMAT_FLOAT2 },
                [ATTR_shape_size] = { .format = SG_VERTEXFORMAT_FLOAT2 },
                [ATTR_shape_radius_border] = { .format = SG_VERTEXFORMAT_FLOAT2 },
                [ATTR_shape_fill_color] = { .format = SG_VERTEXFORMAT_UBYTE4N },
                [ATTR_shape_border_color] = { .format = SG_VERTEXFORMAT_UBYTE4N }
            }
        },
        .shader = sg_make_shader(shape_shader_desc(sg_query_backend())),
        .index_type = SG_INDEXTYPE_UINT16,
        .colors[0].blend = {
            .enabled = true,
            .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
            .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .src_factor_alpha = SG_BLENDFACTOR_ONE,
            .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA
        }
    };
    s_rhi.shape_pip = sg_make_pipeline(&shape_pip_desc);
    
    /* 4. Text Pipeline */
    sg_pipeline_desc text_pip_desc = {
        .layout = {
            .attrs = {
                [ATTR_text_pos] = { .format = SG_VERTEXFORMAT_FLOAT2 },
                [ATTR_text_uv] = { .format = SG_VERTEXFORMAT_FLOAT2 },
                [ATTR_text_color] = { .format = SG_VERTEXFORMAT_UBYTE4N }
            }
        },
        .shader = sg_make_shader(text_shader_desc(sg_query_backend())),
        .index_type = SG_INDEXTYPE_UINT16,
        .colors[0].blend = shape_pip_desc.colors[0].blend
    };
    s_rhi.text_pip = sg_make_pipeline(&text_pip_desc);
    
    printf("[Chitra:Sokol] RHI Initialized (Retained Batcher Ready).\n");
}

static void rhi_begin_frame(int width, int height) {
    s_rhi.current_width = width;
    s_rhi.current_height = height;
    
    sg_pass_action pass_action = {
        .colors[0] = { .load_action = SG_LOADACTION_CLEAR, .clear_value = { 0.1f, 0.1f, 0.1f, 1.0f } }
    };
    sg_begin_pass(&(sg_pass){ .action = pass_action, .swapchain = sglue_swapchain() });
}

static void rhi_upload_vertices(const void* shape_data, size_t shape_size,
                                const void* text_data, size_t text_size,
                                const uint16_t* index_data, size_t index_size) {
    
    if (shape_size > 0) {
        sg_write_buffer_transient(&(sg_write_buffer_desc){
            .src.data = { .ptr = shape_data, .size = shape_size },
            .dst.buffer = s_rhi.shape_staging
        });
        sg_copy_buffer_to_buffer(&(sg_copy_buffer_to_buffer_desc){
            .src = { .buffer = s_rhi.shape_staging },
            .dst = { .buffer = s_rhi.shape_bind.vertex_buffers[0] },
            .size = shape_size
        });
    }
    
    if (text_size > 0) {
        sg_write_buffer_transient(&(sg_write_buffer_desc){
            .src.data = { .ptr = text_data, .size = text_size },
            .dst.buffer = s_rhi.text_staging
        });
        sg_copy_buffer_to_buffer(&(sg_copy_buffer_to_buffer_desc){
            .src = { .buffer = s_rhi.text_staging },
            .dst = { .buffer = s_rhi.text_bind.vertex_buffers[0] },
            .size = text_size
        });
    }
    
    if (index_size > 0) {
        sg_write_buffer_transient(&(sg_write_buffer_desc){
            .src.data = { .ptr = index_data, .size = index_size },
            .dst.buffer = s_rhi.index_staging
        });
        sg_copy_buffer_to_buffer(&(sg_copy_buffer_to_buffer_desc){
            .src = { .buffer = s_rhi.index_staging },
            .dst = { .buffer = s_rhi.shape_bind.index_buffer },
            .size = index_size
        });
    }
}

static void rhi_set_scissor(int x, int y, int w, int h) {
    sg_apply_scissor_rect(x, y, w, h, true);
}

static void rhi_draw_batch(const Chitra_DrawBatch* batch) {
    if (batch->pipeline == CHITRA_PIPELINE_SHAPE) {
        sg_apply_pipeline(s_rhi.shape_pip);
        sg_apply_bindings(&s_rhi.shape_bind);
        
        vs_params_t vs_params = { .u_resolution = { (float)s_rhi.current_width, (float)s_rhi.current_height } };
        sg_apply_uniforms(UB_vs_params, &SG_RANGE(vs_params));
        
        sg_draw((int)batch->base_element, (int)batch->num_elements, 1);
    }
    else if (batch->pipeline == CHITRA_PIPELINE_TEXTURE) {
        /* TODO: Bind texture atlas based on batch->texture_id */
        sg_apply_pipeline(s_rhi.text_pip);
        sg_apply_bindings(&s_rhi.text_bind);
        
        text_vs_params_t vs_params = { .u_resolution = { (float)s_rhi.current_width, (float)s_rhi.current_height } };
        sg_apply_uniforms(UB_text_vs_params, &SG_RANGE(vs_params));
        
        sg_draw((int)batch->base_element, (int)batch->num_elements, 1);
    }
}

static void rhi_end_frame(void) {
    sg_end_pass();
    sg_commit();
}

static void rhi_cleanup(void) {
    sg_shutdown();
}

static const Chitra_RHI_API s_api = {
    .init = rhi_init,
    .begin_frame = rhi_begin_frame,
    .upload_vertices = rhi_upload_vertices,
    .set_scissor = rhi_set_scissor,
    .draw_batch = rhi_draw_batch,
    .end_frame = rhi_end_frame,
    .cleanup = rhi_cleanup
};

const Chitra_RHI_API* chitra_rhi_get_api(void) {
    return &s_api;
}
