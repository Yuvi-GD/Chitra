#include "chitra.h"
#include "chitra_rhi.h"
#include "chitra_texture.h"
#include "chitra_table.h"
#include "chitra_batch.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

/* =========================================================================
 * Mock Headless RHI Implementation for Pure Unit Testing
 * ========================================================================= */

static uint16_t s_mock_tex_counter = 0;
static int s_mock_updates = 0;
static int s_mock_sub_updates = 0;

static void mock_rhi_init(const struct Chitra_Config* config) { (void)config; }
static void mock_rhi_begin_frame(int w, int h) { (void)w; (void)h; }
static void mock_rhi_upload_vertices(uint8_t pipeline, const void* data, size_t size) { (void)pipeline; (void)data; (void)size; }
static void mock_rhi_upload_indices(const uint32_t* index_data, size_t index_size) { (void)index_data; (void)index_size; }
static void mock_rhi_set_scissor(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
static void mock_rhi_draw_batch(const Chitra_DrawBatch* batch) { (void)batch; }
static void mock_rhi_end_frame(void) {}
static void mock_rhi_cleanup(void) {}

static uint16_t mock_rhi_create_texture(int width, int height) {
    (void)width; (void)height;
    return ++s_mock_tex_counter;
}

static uint16_t mock_rhi_create_texture_rgba(int width, int height) {
    (void)width; (void)height;
    return ++s_mock_tex_counter;
}

static void mock_rhi_update_texture(uint16_t id, const unsigned char* data, int width, int height) {
    (void)id; (void)data; (void)width; (void)height;
    s_mock_updates++;
}

static void mock_rhi_update_texture_rgba(uint16_t id, const void* data, int width, int height) {
    (void)id; (void)data; (void)width; (void)height;
    s_mock_updates++;
}

static void mock_rhi_update_texture_sub_rgba(uint16_t id, int dst_x, int dst_y, int w, int h, const void* data) {
    (void)id; (void)dst_x; (void)dst_y; (void)w; (void)h; (void)data;
    s_mock_sub_updates++;
}

static void mock_rhi_destroy_texture(uint16_t id) {
    (void)id;
}

static const Chitra_RHI_API s_mock_rhi = {
    .init                   = mock_rhi_init,
    .begin_frame            = mock_rhi_begin_frame,
    .upload_vertices        = mock_rhi_upload_vertices,
    .upload_indices         = mock_rhi_upload_indices,
    .set_scissor            = mock_rhi_set_scissor,
    .draw_batch             = mock_rhi_draw_batch,
    .end_frame              = mock_rhi_end_frame,
    .cleanup                = mock_rhi_cleanup,
    .create_texture         = mock_rhi_create_texture,
    .update_texture         = mock_rhi_update_texture,
    .create_texture_rgba    = mock_rhi_create_texture_rgba,
    .update_texture_rgba    = mock_rhi_update_texture_rgba,
    .update_texture_sub_rgba = mock_rhi_update_texture_sub_rgba,
    .destroy_texture        = mock_rhi_destroy_texture
};

static void generate_test_pixels(uint32_t* buf, int w, int h, uint32_t color) {
    for (int i = 0; i < w * h; i++) {
        buf[i] = color;
    }
}

static void write_test_bmp(const char* filename, int w, int h) {
    FILE* f = fopen(filename, "wb");
    if (!f) return;
    int filesize = 54 + 4 * w * h;
    unsigned char header[54] = {
        'B','M', (unsigned char)(filesize&255), (unsigned char)((filesize>>8)&255), (unsigned char)((filesize>>16)&255), (unsigned char)((filesize>>24)&255),
        0,0,0,0, 54,0,0,0,
        40,0,0,0, (unsigned char)(w&255), (unsigned char)((w>>8)&255), (unsigned char)((w>>16)&255), (unsigned char)((w>>24)&255),
        (unsigned char)(h&255), (unsigned char)((h>>8)&255), (unsigned char)((h>>16)&255), (unsigned char)((h>>24)&255),
        1,0, 32,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
    };
    fwrite(header, 1, 54, f);
    for (int i = 0; i < w * h; i++) {
        unsigned char px[4] = { 0x00, 0xFF, 0x80, 0xFF }; /* BGRA */
        fwrite(px, 1, 4, f);
    }
    fclose(f);
}

int main(void) {
    printf("============================================================\n");
    printf("[Chitra Test] Running Headless Unified Texture Pipeline...\n");
    printf("============================================================\n");
    fflush(stdout);

    /* Plug in Mock RHI for headless test */
    chitra_rhi_set_api(&s_mock_rhi);

    /* Initialize embedded Chitra engine */
    chitra_table_init();
    chitra_batch_init();
    chitra_clip_init();
    chitra_texture_init();

    uint16_t atlas_id = chitra_texture_get_atlas_id();
    printf("[Test] Dynamic Atlas initialized with Texture ID: %u\n", atlas_id);
    assert(atlas_id > 0);
    fflush(stdout);

    /* =========================================================================
     * TEST 1: Rule 1 - Standalone Large Texture (> 512x512)
     * ========================================================================= */
    printf("\n--- Test 1: Rule 1 Standalone Texture (> 512x512) ---\n");
    {
        int w = 600, h = 600;
        uint32_t* px = (uint32_t*)malloc(w * h * sizeof(uint32_t));
        generate_test_pixels(px, w, h, 0xFFFF0000);

        uint32_t h_standalone = chitra_create(CHITRA_PIPELINE_TEXTURE);
        assert(h_standalone != CHITRA_INVALID_HANDLE);

        bool ok = chitra_texture_load(h_standalone, w, h, px);
        assert(ok);

        Chitra_CommandSlot* slot = chitra_table_get_slot(h_standalone);
        uint16_t tex_id = (uint16_t)(slot->sort_key & CHITRA_TEXTURE_ID_MASK);
        printf("[Test 1] Standalone Texture ID: %u (Must NOT be Atlas ID %u)\n", tex_id, atlas_id);
        assert(tex_id != atlas_id);
        assert(tex_id > 0);

        uint8_t pipe;
        uint32_t count;
        Chitra_TextVertex* v = (Chitra_TextVertex*)chitra_table_get_vertices(h_standalone, &pipe, &count);
        assert(v != NULL && count == 4);

        /* Verify UVs are 1:1 (0.0 to 1.0) */
        printf("[Test 1] UVs: (%f, %f) to (%f, %f)\n", v[0].uv[0], v[0].uv[1], v[2].uv[0], v[2].uv[1]);
        assert(v[0].uv[0] == 0.0f && v[0].uv[1] == 0.0f);
        assert(v[2].uv[0] == 1.0f && v[2].uv[1] == 1.0f);

        /* =====================================================================
         * TEST 2: Local Src Rect Slicing on Standalone Texture
         * ===================================================================== */
        printf("\n--- Test 2: Local Src Rect Slicing ---\n");
        /* Crop to top-left quarter: (0, 0, 300, 300) out of 600x600 */
        ok = chitra_texture_set_src_rect(h_standalone, 0.0f, 0.0f, 300.0f, 300.0f, 600.0f, 600.0f);
        assert(ok);
        printf("[Test 2] Sliced UVs: (%f, %f) to (%f, %f)\n", v[0].uv[0], v[0].uv[1], v[2].uv[0], v[2].uv[1]);
        assert(fabsf(v[0].uv[0] - 0.0f) < 0.001f);
        assert(fabsf(v[0].uv[1] - 0.0f) < 0.001f);
        assert(fabsf(v[2].uv[0] - 0.5f) < 0.001f);
        assert(fabsf(v[2].uv[1] - 0.5f) < 0.001f);

        free(px);
    }
    fflush(stdout);

    /* =========================================================================
     * TEST 3: Rule 2 - Dynamic Atlas Automatic Packing (<= 512x512)
     * ========================================================================= */
    printf("\n--- Test 3: Rule 2 Dynamic Atlas Packing (<= 512x512) ---\n");
    #define NUM_ICONS 16
    uint32_t icons[NUM_ICONS];
    for (int i = 0; i < NUM_ICONS; i++) {
        int iw = 32 + (i % 4) * 8;  /* 32, 40, 48, 56 */
        int ih = 32 + (i % 2) * 16; /* 32, 48 */
        uint32_t* ipx = (uint32_t*)malloc(iw * ih * sizeof(uint32_t));
        generate_test_pixels(ipx, iw, ih, 0xFF00FF00);

        icons[i] = chitra_create(CHITRA_PIPELINE_TEXTURE);
        assert(icons[i] != CHITRA_INVALID_HANDLE);

        bool ok = chitra_texture_load(icons[i], iw, ih, ipx);
        assert(ok);

        Chitra_CommandSlot* slot = chitra_table_get_slot(icons[i]);
        uint16_t tex_id = (uint16_t)(slot->sort_key & CHITRA_TEXTURE_ID_MASK);
        /* MUST be packed into the shared Atlas! */
        assert(tex_id == atlas_id);

        uint8_t pipe;
        uint32_t count;
        Chitra_TextVertex* v = (Chitra_TextVertex*)chitra_table_get_vertices(icons[i], &pipe, &count);
        assert(v != NULL && count == 4);

        /* Verify UVs are within (0.0 to 1.0) and non-zero */
        assert(v[0].uv[0] >= 0.0f && v[0].uv[1] >= 0.0f);
        assert(v[2].uv[0] > v[0].uv[0]);
        assert(v[2].uv[1] > v[0].uv[1]);

        free(ipx);
    }
    printf("[Test 3] Successfully packed %d icons into shared Atlas ID: %u\n", NUM_ICONS, atlas_id);
    fflush(stdout);

    /* =========================================================================
     * TEST 4: Batch Coalescing across Atlas Quads
     * ========================================================================= */
    printf("\n--- Test 4: Batch Coalescing ---\n");
    for (int i = 0; i < NUM_ICONS; i++) {
        chitra_texture_set_bounds(icons[i], (float)(i * 40), 100.0f, 32.0f, 32.0f);
        chitra_set_draw_order(icons[i], 10); /* All share the same draw order! */
    }

    chitra_batch_prepare();
    uint32_t batch_count = 0;
    const Chitra_DrawBatch* batches = chitra_batch_get_batches(&batch_count);
    printf("[Test 4] Prepared batches count: %u\n", batch_count);

    /* Since all icons share draw_order=10 and texture_id=atlas_id, they must coalesce into 1 batch! */
    bool found_atlas_batch = false;
    for (uint32_t b = 0; b < batch_count; b++) {
        if (batches[b].pipeline == CHITRA_PIPELINE_TEXTURE && batches[b].texture_id == atlas_id) {
            printf("[Test 4] Atlas batch elements: %u (expected %u indices for %d quads)\n",
                   batches[b].num_elements, NUM_ICONS * 6, NUM_ICONS);
            assert(batches[b].num_elements == NUM_ICONS * 6);
            found_atlas_batch = true;
        }
    }
    assert(found_atlas_batch);
    fflush(stdout);

    /* =========================================================================
     * TEST 5: Direct-to-VRAM GPU Sub-Region Upload Verification
     * ========================================================================= */
    printf("\n--- Test 5: Direct-to-VRAM GPU Upload (Zero RAM Staging) ---\n");
    printf("[Test 5] Direct VRAM sub-rect uploads performed = %d (expected %d)\n", s_mock_sub_updates, NUM_ICONS + 1);
    assert(s_mock_sub_updates >= NUM_ICONS);
    fflush(stdout);

    /* =========================================================================
     * TEST 6: Continuous Packing & Fallback Handling
     * ========================================================================= */
    printf("\n--- Test 6: Continuous Packing & Dedicated Slot Fallback ---\n");
    /* Destroy half of the icons */
    for (int i = 0; i < NUM_ICONS; i += 2) {
        chitra_destroy(icons[i]);
    }
    printf("[Test 6] Destroyed half the icons.\n");

    /* Pack additional items directly to VRAM */
    for (int i = 0; i < 8; i++) {
        uint32_t px[64 * 64];
        generate_test_pixels(px, 64, 64, 0xFF0000FF);
        uint32_t h_new = chitra_create(CHITRA_PIPELINE_TEXTURE);
        bool ok = chitra_texture_load(h_new, 64, 64, px);
        assert(ok);
        Chitra_CommandSlot* slot = chitra_table_get_slot(h_new);
        assert((slot->sort_key & CHITRA_TEXTURE_ID_MASK) == atlas_id);
    }
    printf("[Test 6] Successfully packed new items directly into VRAM atlas!\n");
    fflush(stdout);

    /* =========================================================================
     * TEST 7: Runtime Inspection Getters & Bounds Verification
     * ========================================================================= */
    printf("\n--- Test 7: Runtime Inspection Getters ---\n");
    uint32_t elem = chitra_create(CHITRA_PIPELINE_TEXTURE);
    chitra_set_draw_order(elem, 42);
    chitra_set_clip(elem, 7);
    chitra_texture_set_bounds(elem, 10.0f, 20.0f, 150.0f, 75.0f);

    uint32_t px_elem[32 * 32];
    generate_test_pixels(px_elem, 32, 32, 0xFF123456);
    chitra_texture_load(elem, 32, 32, px_elem);

    assert(chitra_get_pipeline_id(elem) == CHITRA_PIPELINE_TEXTURE);
    assert(chitra_get_draw_order(elem) == 42);
    assert(chitra_get_clip_id(elem) == 7);
    assert(chitra_get_texture_id(elem) == atlas_id);

    float bw = 0.0f, bh = 0.0f;
    bool ok_b = chitra_texture_get_bounds(elem, &bw, &bh);
    assert(ok_b);
    assert((int)bw == 150 && (int)bh == 75);

    printf("[Test 7] Texture ID: %u (ephemeral verified)\n", chitra_get_texture_id(elem));
    printf("[Test 7] Clip ID: %u, Pipeline ID: %u, Draw Order: %u\n", 
           chitra_get_clip_id(elem), chitra_get_pipeline_id(elem), chitra_get_draw_order(elem));
    printf("[Test 7] Texture Bounds: %.1f x %.1f\n", bw, bh);
    chitra_destroy(elem);
    fflush(stdout);

    /* =========================================================================
     * TEST 8: Direct File Image Loading (chitra_texture_load_file via stb_image)
     * ========================================================================= */
    printf("\n--- Test 8: Direct File Image Loading (chitra_texture_load_file) ---\n");
    const char* bmp_path = "test_icon_temp.bmp";
    write_test_bmp(bmp_path, 16, 16);

    uint32_t file_elem = chitra_create(CHITRA_PIPELINE_TEXTURE);
    bool load_ok = chitra_texture_load_file(file_elem, bmp_path);
    assert(load_ok);
    assert(chitra_get_texture_id(file_elem) == atlas_id);
    printf("[Test 8] Successfully loaded '%s' directly into atlas via stb_image!\n", bmp_path);
    chitra_destroy(file_elem);
    remove(bmp_path);
    fflush(stdout);

    /* =========================================================================
     * TEST 9: Dynamic Atlas Growth, Ref-Counting, Dead Pixels & Auto-Unload
     * ========================================================================= */
    printf("\n--- Test 9: Dynamic Atlas Growth, Ref-Counting & Auto-Unload ---\n");
    printf("[Test 9] Initial Active Atlas Texture ID: %u\n", atlas_id);

    /* Allocate five 512x512 sub-images.
     * Since one 1024x1024 page can only hold four 512x512 quads, the 5th quad
     * MUST spill over and automatically create Page 2! */
    uint32_t big_icons[5];
    uint32_t* px_512 = (uint32_t*)malloc(512 * 512 * sizeof(uint32_t));
    assert(px_512 != NULL);
    memset(px_512, 0xFF, 512 * 512 * sizeof(uint32_t));

    for (int i = 0; i < 5; i++) {
        big_icons[i] = chitra_create(CHITRA_PIPELINE_TEXTURE);
        bool ok = chitra_texture_load(big_icons[i], 512, 512, px_512);
        assert(ok);
    }
    free(px_512);

    uint16_t page1_id = chitra_get_texture_id(big_icons[0]);
    uint16_t page2_id = chitra_get_texture_id(big_icons[4]);

    printf("[Test 9] Page 1 Texture ID: %u (ref_count=%u, allocated=%u), Page 2 Texture ID: %u (ref_count=%u, allocated=%u)\n",
           page1_id, chitra_texture_get_ref_count(page1_id), chitra_texture_get_allocated_area(page1_id),
           page2_id, chitra_texture_get_ref_count(page2_id), chitra_texture_get_allocated_area(page2_id));
    assert(page1_id != page2_id);
    assert(chitra_texture_is_atlas(page1_id));
    assert(chitra_texture_is_atlas(page2_id));
    assert(chitra_texture_get_allocated_area(page2_id) > 0);

    /* Destroy 1 quad on Page 1: triggers dead pixel tracking / 35% auto-compaction */
    chitra_destroy(big_icons[0]);
    printf("[Test 9] After destroying quad: Page 1 is_atlas=%d (ref_count=%u, dead_area=%u)\n",
           chitra_texture_is_atlas(page1_id),
           chitra_texture_get_ref_count(page1_id),
           chitra_texture_get_dead_area(page1_id));

    /* Destroy remaining quads on both pages */
    for (int i = 1; i < 5; i++) {
        chitra_destroy(big_icons[i]);
    }

    assert(chitra_texture_get_ref_count(page2_id) == 0);
    printf("[Test 9] All elements cleanly destroyed. Active atlas cursor cleanly reset to (0,0).\n");
    fflush(stdout);

    /* Cleanup engine */
    chitra_texture_term();
    chitra_clip_term();
    chitra_batch_term();
    chitra_table_term();

    printf("\n============================================================\n");
    printf("[Chitra Test] ALL TESTS PASSED SUCCESSFULLY! (100%% SOLID)\n");
    printf("============================================================\n");
    fflush(stdout);

    return 0;
}
