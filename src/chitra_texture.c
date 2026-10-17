#include "chitra_texture.h"
#include "chitra_table.h"
#include "chitra_rhi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

/* =========================================================================
 * Dynamic RGBA Atlas State (Single Active Cursor, 0-Byte CPU Staging)
 * ========================================================================= */

#define CHITRA_ATLAS_SIZE  1024
#define CHITRA_ATLAS_NODES 1024

typedef struct {
    uint16_t      texture_id;
    stbrp_context packer;
    stbrp_node*   nodes; /* Scratch buffer allocated ONCE at startup (16 KB) */
} Chitra_ActiveAtlas;

typedef struct {
    uint16_t ref_count;      /* Active living quads using this texture */
    uint32_t allocated_area; /* Sum of pixel area packed */
    uint32_t dead_area;      /* Sum of pixel area freed */
    bool     is_atlas;       /* true = dynamic atlas, false = standalone */
    bool     is_evacuating;  /* Re-entrancy guard */
} Chitra_TextureMeta;

static Chitra_ActiveAtlas  s_active_atlas      = {0};
static Chitra_TextureMeta* s_tex_meta          = NULL;
static uint16_t            s_tex_meta_capacity = 0;
static bool                s_atlas_initialized = false;

/* Dynamic 1.5X Growth matching Chitra core */
static void ensure_tex_meta_capacity(uint16_t required_id) {
    if (required_id < s_tex_meta_capacity) return;

    uint16_t new_cap = s_tex_meta_capacity ? (s_tex_meta_capacity + (s_tex_meta_capacity >> 1)) : 16;
    while (new_cap <= required_id) {
        new_cap = new_cap + (new_cap >> 1);
        if (new_cap <= s_tex_meta_capacity) {
            new_cap = required_id + 1;
            break;
        }
    }

    Chitra_TextureMeta* new_meta = (Chitra_TextureMeta*)realloc(
        s_tex_meta, new_cap * sizeof(Chitra_TextureMeta)
    );
    if (!new_meta) return;

    memset(new_meta + s_tex_meta_capacity, 0, (new_cap - s_tex_meta_capacity) * sizeof(Chitra_TextureMeta));
    s_tex_meta = new_meta;
    s_tex_meta_capacity = new_cap;
}

/* =========================================================================
 * Subsystem Lifecycle
 * ========================================================================= */

void chitra_texture_init(void) {
    if (s_atlas_initialized) return;

    /* 1. Allocate 1024 skyline nodes ONCE at startup (reused across all future atlases) */
    s_active_atlas.nodes = (stbrp_node*)malloc(sizeof(stbrp_node) * CHITRA_ATLAS_NODES);
    stbrp_init_target(&s_active_atlas.packer, CHITRA_ATLAS_SIZE, CHITRA_ATLAS_SIZE, s_active_atlas.nodes, CHITRA_ATLAS_NODES);

    /* 2. Create initial GPU atlas texture */
    const Chitra_RHI_API* rhi = chitra_rhi_get_api();
    if (rhi && rhi->create_texture_rgba) {
        s_active_atlas.texture_id = rhi->create_texture_rgba(CHITRA_ATLAS_SIZE, CHITRA_ATLAS_SIZE);
        ensure_tex_meta_capacity(s_active_atlas.texture_id);
        s_tex_meta[s_active_atlas.texture_id].is_atlas = true;
    }

    s_atlas_initialized = true;
}

void chitra_texture_term(void) {
    const Chitra_RHI_API* rhi = chitra_rhi_get_api();
    if (rhi && rhi->destroy_texture && s_tex_meta) {
        for (uint16_t id = 0; id < s_tex_meta_capacity; id++) {
            if (s_tex_meta[id].ref_count > 0 || (s_tex_meta[id].is_atlas && id == s_active_atlas.texture_id)) {
                rhi->destroy_texture(id);
            }
        }
    }
    if (s_active_atlas.nodes) {
        free(s_active_atlas.nodes);
        s_active_atlas.nodes = NULL;
    }
    if (s_tex_meta) {
        free(s_tex_meta);
        s_tex_meta = NULL;
        s_tex_meta_capacity = 0;
    }
    s_active_atlas.texture_id = 0;
    s_atlas_initialized = false;
}

uint16_t chitra_texture_get_atlas_id(void) {
    return s_active_atlas.texture_id;
}

bool chitra_texture_is_atlas(uint16_t tex_id) {
    if (tex_id == 0 || tex_id >= s_tex_meta_capacity || !s_tex_meta) return false;
    return s_tex_meta[tex_id].is_atlas;
}

uint32_t chitra_texture_get_ref_count(uint16_t tex_id) {
    if (tex_id == 0 || tex_id >= s_tex_meta_capacity || !s_tex_meta) return 0;
    return s_tex_meta[tex_id].ref_count;
}

uint32_t chitra_texture_get_allocated_area(uint16_t tex_id) {
    if (tex_id == 0 || tex_id >= s_tex_meta_capacity || !s_tex_meta) return 0;
    return s_tex_meta[tex_id].allocated_area;
}

uint32_t chitra_texture_get_dead_area(uint16_t tex_id) {
    if (tex_id == 0 || tex_id >= s_tex_meta_capacity || !s_tex_meta) return 0;
    return s_tex_meta[tex_id].dead_area;
}

void chitra_texture_prepare(void) {
    /* NO-OP: Textures are uploaded directly to GPU VRAM at load time */
}

/* =========================================================================
 * On-Destroy Hook (Invoked by chitra_destroy)
 * ========================================================================= */

void chitra_texture_on_destroy(uint32_t handle) {
    Chitra_CommandSlot* slot = chitra_table_get_slot(handle);
    if (!slot) return;

    uint8_t pipe = (uint8_t)((slot->sort_key >> 13) & CHITRA_PIPELINE_MASK);
    if (pipe != CHITRA_PIPELINE_TEXTURE) return;

    uint16_t tex_id = (uint16_t)(slot->sort_key & CHITRA_TEXTURE_ID_MASK);
    if (tex_id == 0 || tex_id >= s_tex_meta_capacity || !s_tex_meta) return;

    Chitra_TextureMeta* meta = &s_tex_meta[tex_id];
    if (meta->is_evacuating) return; /* RE-ENTRANCY GUARD: prevent infinite recursion! */

    const Chitra_RHI_API* rhi = chitra_rhi_get_api();
    if (!rhi) return;

    /* Decrement reference count */
    if (meta->ref_count > 0) {
        meta->ref_count--;
    }

    /* Compute freed pixel area from UV bounds for atlas textures */
    uint32_t count = 0;
    uint8_t p = 0;
    Chitra_TextVertex* v = (Chitra_TextVertex*)chitra_table_get_vertices(handle, &p, &count);
    if (v && count == 4 && meta->is_atlas) {
        float du = v[2].uv[0] - v[0].uv[0];
        float dv = v[2].uv[1] - v[0].uv[1];
        if (du > 0.0f && dv > 0.0f) {
            uint32_t freed_w = (uint32_t)(du * CHITRA_ATLAS_SIZE + 0.5f);
            uint32_t freed_h = (uint32_t)(dv * CHITRA_ATLAS_SIZE + 0.5f);
            meta->dead_area += freed_w * freed_h;
        }
    }

    /* Condition 1: Reference count dropped to 0 (No quads using this texture) */
    if (meta->ref_count == 0) {
        if (tex_id != s_active_atlas.texture_id) {
            /* Old atlas page or standalone texture: destroy GPU texture immediately */
            if (rhi->destroy_texture) {
                rhi->destroy_texture(tex_id);
            }
            memset(meta, 0, sizeof(Chitra_TextureMeta));
        } else {
            /* Active atlas: reset packing cursor to (0,0) (zero dead pixels, zero GPU recreate) */
            stbrp_init_target(&s_active_atlas.packer, CHITRA_ATLAS_SIZE, CHITRA_ATLAS_SIZE, s_active_atlas.nodes, CHITRA_ATLAS_NODES);
            meta->allocated_area = 0;
            meta->dead_area = 0;
        }
        return;
    }

    /* Condition 2: Dead pixel threshold >= 35% on an old atlas page */
    if (meta->is_atlas && tex_id != s_active_atlas.texture_id && meta->allocated_area > 0) {
        float dead_ratio = (float)meta->dead_area / (float)meta->allocated_area;
        if (dead_ratio >= 0.35f) {
            /* Mark as evacuating to prevent any recursive re-entry */
            meta->is_evacuating = true;

            /* When remaining elements are cleaned or migrated, destroy this texture */
            if (rhi->destroy_texture) {
                rhi->destroy_texture(tex_id);
            }
            memset(meta, 0, sizeof(Chitra_TextureMeta));
        }
    }
}

/* =========================================================================
 * Unified Texture Loader (Rules 1 & 2 - Direct to VRAM)
 * ========================================================================= */

bool chitra_texture_load(uint32_t handle, int width, int height, const void* rgba_pixels) {
    if (width <= 0 || height <= 0 || !rgba_pixels) return false;

    uint8_t pipe;
    uint32_t count;
    Chitra_TextVertex* v = (Chitra_TextVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_TEXTURE || count != 4) return false;

    const Chitra_RHI_API* rhi = chitra_rhi_get_api();
    if (!rhi) return false;

    /* Rule 1: Large Images (> 512x512) -> Standalone GPU Texture */
    if (width > 512 || height > 512) {
        uint16_t tex_id = rhi->create_texture_rgba(width, height);
        if (tex_id == 0) return false;

        ensure_tex_meta_capacity(tex_id);
        s_tex_meta[tex_id].ref_count = 1;
        s_tex_meta[tex_id].allocated_area = (uint32_t)(width * height);
        s_tex_meta[tex_id].dead_area = 0;
        s_tex_meta[tex_id].is_atlas = false;
        s_tex_meta[tex_id].is_evacuating = false;

        rhi->update_texture_rgba(tex_id, rgba_pixels, width, height);
        chitra_set_texture(handle, tex_id);

        /* Full 1:1 UV coordinates */
        v[0].uv[0] = 0.0f; v[0].uv[1] = 0.0f;
        v[1].uv[0] = 1.0f; v[1].uv[1] = 0.0f;
        v[2].uv[0] = 1.0f; v[2].uv[1] = 1.0f;
        v[3].uv[0] = 0.0f; v[3].uv[1] = 1.0f;

        chitra_table_mark_dirty();
        return true;
    }

    /* Rule 2: Small Images (<= 512x512) -> Dynamic Atlas Direct-to-VRAM Packing */
    if (!s_atlas_initialized) {
        chitra_texture_init();
    }

    stbrp_rect rect;
    rect.id = (int)handle;
    rect.w = width;
    rect.h = height;

    int packed = stbrp_pack_rects(&s_active_atlas.packer, &rect, 1);

    if (!packed) {
        /* Active atlas is full! Allocate a brand new atlas texture from RHI */
        uint16_t new_id = rhi->create_texture_rgba(CHITRA_ATLAS_SIZE, CHITRA_ATLAS_SIZE);
        if (new_id == 0) return false;

        ensure_tex_meta_capacity(new_id);
        s_tex_meta[new_id].is_atlas = true;
        s_tex_meta[new_id].ref_count = 0;
        s_tex_meta[new_id].allocated_area = 0;
        s_tex_meta[new_id].dead_area = 0;
        s_tex_meta[new_id].is_evacuating = false;

        s_active_atlas.texture_id = new_id;
        stbrp_init_target(&s_active_atlas.packer, CHITRA_ATLAS_SIZE, CHITRA_ATLAS_SIZE, s_active_atlas.nodes, CHITRA_ATLAS_NODES);

        packed = stbrp_pack_rects(&s_active_atlas.packer, &rect, 1);
        if (!packed) {
            /* Still doesn't fit (larger than atlas): fallback to standalone */
            rhi->update_texture_rgba(new_id, rgba_pixels, width, height);
            chitra_set_texture(handle, new_id);
            s_tex_meta[new_id].ref_count = 1;
            s_tex_meta[new_id].is_atlas = false;
            v[0].uv[0] = 0.0f; v[0].uv[1] = 0.0f;
            v[1].uv[0] = 1.0f; v[1].uv[1] = 0.0f;
            v[2].uv[0] = 1.0f; v[2].uv[1] = 1.0f;
            v[3].uv[0] = 0.0f; v[3].uv[1] = 1.0f;
            chitra_table_mark_dirty();
            return true;
        }
    }

    uint16_t cur_id = s_active_atlas.texture_id;
    ensure_tex_meta_capacity(cur_id);
    s_tex_meta[cur_id].ref_count++;
    s_tex_meta[cur_id].allocated_area += (uint32_t)(width * height);

    /* DIRECT GPU UPLOAD: Zero CPU RAM Staging! */
    if (rhi->update_texture_sub_rgba) {
        rhi->update_texture_sub_rgba(cur_id, rect.x, rect.y, width, height, rgba_pixels);
    }

    /* Bind element to atlas texture ID */
    chitra_set_texture(handle, cur_id);

    /* Write normalized UV coordinates directly to element's 4 vertices */
    float inv_size = 1.0f / (float)CHITRA_ATLAS_SIZE;
    float u0 = (float)rect.x * inv_size;
    float v0 = (float)rect.y * inv_size;
    float u1 = (float)(rect.x + width) * inv_size;
    float v1 = (float)(rect.y + height) * inv_size;

    v[0].uv[0] = u0; v[0].uv[1] = v0;
    v[1].uv[0] = u1; v[1].uv[1] = v0;
    v[2].uv[0] = u1; v[2].uv[1] = v1;
    v[3].uv[0] = u0; v[3].uv[1] = v1;

    chitra_table_mark_dirty();
    return true;
}

bool chitra_texture_load_file(uint32_t handle, const char* file_path) {
    if (!file_path || !file_path[0]) return false;

    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load(file_path, &w, &h, &comp, 4); /* Force 4-channel RGBA */
    if (!pixels) {
        fprintf(stderr, "[Chitra:Texture] Failed to load image from '%s': %s\n", file_path, stbi_failure_reason());
        return false;
    }

    bool success = chitra_texture_load(handle, w, h, pixels);
    stbi_image_free(pixels);
    return success;
}

/* =========================================================================
 * Spritesheet / Local Rect UV Mapping
 * ========================================================================= */

bool chitra_texture_set_src_rect(uint32_t handle, float src_x, float src_y, float src_w, float src_h, float img_w, float img_h) {
    if (img_w <= 0.0f || img_h <= 0.0f) return false;

    uint8_t pipe;
    uint32_t count;
    Chitra_TextVertex* v = (Chitra_TextVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_TEXTURE || count != 4) return false;

    Chitra_CommandSlot* slot = chitra_table_get_slot(handle);
    if (!slot) return false;

    uint16_t tex_id = (uint16_t)(slot->sort_key & CHITRA_TEXTURE_ID_MASK);

    float rel_u0 = src_x / img_w;
    float rel_v0 = src_y / img_h;
    float rel_u1 = (src_x + src_w) / img_w;
    float rel_v1 = (src_y + src_h) / img_h;

    if (chitra_texture_is_atlas(tex_id)) {
        /* Dynamic atlas: scale within the allocated sub-rect bounding box */
        float base_u0 = v[0].uv[0];
        float base_v0 = v[0].uv[1];
        float base_u1 = v[2].uv[0];
        float base_v1 = v[2].uv[1];

        float du = base_u1 - base_u0;
        float dv = base_v1 - base_v0;

        v[0].uv[0] = base_u0 + rel_u0 * du;
        v[0].uv[1] = base_v0 + rel_v0 * dv;

        v[1].uv[0] = base_u0 + rel_u1 * du;
        v[1].uv[1] = base_v0 + rel_v0 * dv;

        v[2].uv[0] = base_u0 + rel_u1 * du;
        v[2].uv[1] = base_v0 + rel_v1 * dv;

        v[3].uv[0] = base_u0 + rel_u0 * du;
        v[3].uv[1] = base_v0 + rel_v1 * dv;
    } else {
        /* Standalone texture: map directly */
        v[0].uv[0] = rel_u0; v[0].uv[1] = rel_v0;
        v[1].uv[0] = rel_u1; v[1].uv[1] = rel_v0;
        v[2].uv[0] = rel_u1; v[2].uv[1] = rel_v1;
        v[3].uv[0] = rel_u0; v[3].uv[1] = rel_v1;
    }

    chitra_table_mark_dirty();
    return true;
}

/* =========================================================================
 * Texture Primitive Setters (Pure O(1) Direct RAM Writes)
 * ========================================================================= */

bool chitra_texture_set_bounds(uint32_t handle, float x, float y, float w, float h) {
    uint8_t pipe;
    uint32_t count;
    Chitra_TextVertex* v = (Chitra_TextVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_TEXTURE || count != 4) return false;

    /* Top-Left */
    v[0].pos[0] = x;     v[0].pos[1] = y;
    /* Top-Right */
    v[1].pos[0] = x + w; v[1].pos[1] = y;
    /* Bottom-Right */
    v[2].pos[0] = x + w; v[2].pos[1] = y + h;
    /* Bottom-Left */
    v[3].pos[0] = x;     v[3].pos[1] = y + h;

    chitra_table_mark_dirty();
    return true;
}

bool chitra_texture_get_bounds(uint32_t handle, float* out_w, float* out_h) {
    uint8_t pipe;
    uint32_t count;
    const Chitra_TextVertex* v = (const Chitra_TextVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_TEXTURE || count != 4) return false;

    if (out_w) *out_w = v[2].pos[0] - v[0].pos[0];
    if (out_h) *out_h = v[2].pos[1] - v[0].pos[1];
    return true;
}

bool chitra_texture_set_uv(uint32_t handle, float u0, float v0, float u1, float v1) {
    uint8_t pipe;
    uint32_t count;
    Chitra_TextVertex* v = (Chitra_TextVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_TEXTURE || count != 4) return false;

    v[0].uv[0] = u0; v[0].uv[1] = v0;
    v[1].uv[0] = u1; v[1].uv[1] = v0;
    v[2].uv[0] = u1; v[2].uv[1] = v1;
    v[3].uv[0] = u0; v[3].uv[1] = v1;

    chitra_table_mark_dirty();
    return true;
}

bool chitra_texture_set_color(uint32_t handle, uint32_t tint_color) {
    uint8_t pipe;
    uint32_t count;
    Chitra_TextVertex* v = (Chitra_TextVertex*)chitra_table_get_vertices(handle, &pipe, &count);
    if (!v || pipe != CHITRA_PIPELINE_TEXTURE || count != 4) return false;

    v[0].color = tint_color;
    v[1].color = tint_color;
    v[2].color = tint_color;
    v[3].color = tint_color;

    chitra_table_mark_dirty();
    return true;
}
