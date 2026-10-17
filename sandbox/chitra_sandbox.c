#include "chitra.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CARDS 12
#define MAX_NAV_ITEMS 8

static int s_font = -1;
static int s_last_w = 0;
static int s_last_h = 0;

static void relayout(int w, int h);

/* Retained Element Handles */
static uint32_t s_h_bg = CHITRA_INVALID_HANDLE;
static uint32_t s_h_sidebar = CHITRA_INVALID_HANDLE;
static uint32_t s_h_logo_text = CHITRA_INVALID_HANDLE;
static uint32_t s_h_topbar = CHITRA_INVALID_HANDLE;
static uint32_t s_h_topbar_title = CHITRA_INVALID_HANDLE;

/* Mode 1: Standalone Large Texture (> 512x512) */
static uint32_t s_h_standalone_banner = CHITRA_INVALID_HANDLE;

/* Mode 2: Sprite Sheet (using chitra_texture_set_src_rect) */
static uint32_t s_h_spritesheet_icon1 = CHITRA_INVALID_HANDLE;
static uint32_t s_h_spritesheet_icon2 = CHITRA_INVALID_HANDLE;

/* Mode 3: Dynamic Atlas Icons (<= 512x512 packed automatically) */
static uint32_t s_h_dynamic_icons[MAX_CARDS];

static uint32_t s_h_nav_bg[MAX_NAV_ITEMS];
static uint32_t s_h_nav_text[MAX_NAV_ITEMS];

static uint32_t s_h_card_bg[MAX_CARDS];
static uint32_t s_h_card_title[MAX_CARDS];
static uint32_t s_h_card_desc[MAX_CARDS];

static int s_hovered_nav = -1;
static bool s_icons_active = true;

/* Helper to generate synthetic procedural test pixels */
static void generate_test_pattern(uint32_t* pixels, int w, int h, uint8_t r_base, uint8_t g_base, uint8_t b_base) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t r = (uint8_t)(r_base + (x * 255 / (w > 1 ? w : 1)));
            uint8_t g = (uint8_t)(g_base + (y * 255 / (h > 1 ? h : 1)));
            uint8_t b = b_base;
            uint8_t a = 255;
            /* Border border highlight */
            if (x == 0 || x == w - 1 || y == 0 || y == h - 1) {
                r = 255; g = 255; b = 255;
            }
            pixels[y * w + x] = (a << 24) | (b << 16) | (g << 8) | r;
        }
    }
}

/* =========================================================================
 * Application Initialization
 * ========================================================================= */

static void on_init(void) {
    printf("[Sandbox] Initializing Chitra V1 Unified Texture Pipeline...\n");

    s_font = chitra_load_font("sans", "../../sandbox/Font/Roboto-Regular.ttf");
    if (s_font == -1) {
        s_font = chitra_load_font("sans", "sandbox/Font/Roboto-Regular.ttf");
    }

    /* 1. Background Panel */
    s_h_bg = chitra_create(CHITRA_PIPELINE_SHAPE);
    chitra_rect_set_bounds(s_h_bg, 0.0f, 0.0f, 1280.0f, 840.0f);
    chitra_rect_set_color(s_h_bg, 0xFF0F0F0F, 0);
    chitra_set_draw_order(s_h_bg, 0);

    /* 2. Sidebar Panel */
    s_h_sidebar = chitra_create(CHITRA_PIPELINE_SHAPE);
    chitra_rect_set_bounds(s_h_sidebar, 0.0f, 0.0f, 260.0f, 840.0f);
    chitra_rect_set_color(s_h_sidebar, 0xFF18181B, 0);
    chitra_set_draw_order(s_h_sidebar, 10);

    /* 3. Sidebar Logo Text */
    if (s_font != -1) {
        s_h_logo_text = chitra_create(CHITRA_PIPELINE_TEXT);
        chitra_text_set(s_h_logo_text, s_font, 22.0f, "Chitra V1 Engine", 25.0f, 24.0f);
        chitra_text_set_color(s_h_logo_text, 0xFFE4E4E7);
        chitra_set_draw_order(s_h_logo_text, 20);
    }

    /* 4. Sidebar Navigation Items */
    for (int i = 0; i < MAX_NAV_ITEMS; i++) {
        float y = 70.0f + (float)i * 42.0f;
        uint32_t bg_col = (i == 0) ? 0xFF27272A : 0xFF18181B;

        s_h_nav_bg[i] = chitra_create(CHITRA_PIPELINE_SHAPE);
        chitra_rect_set_bounds(s_h_nav_bg[i], 16.0f, y, 228.0f, 34.0f);
        chitra_rect_set_radius(s_h_nav_bg[i], 6.0f);
        chitra_rect_set_color(s_h_nav_bg[i], bg_col, 0);
        chitra_set_draw_order(s_h_nav_bg[i], 15);

        if (s_font != -1) {
            const char* nav_names[] = {
                "Texture Showcase", "Dynamic Atlas", "Compaction Demo", 
                "Standalone 600px", "Spritesheet Slice", "Batch Coalescing", 
                "Layout Grid", "Press SPACE to Defrag"
            };
            s_h_nav_text[i] = chitra_create(CHITRA_PIPELINE_TEXT);
            chitra_text_set(s_h_nav_text[i], s_font, 14.0f, nav_names[i % 8], 30.0f, y + 8.0f);
            chitra_text_set_color(s_h_nav_text[i], (i == 0) ? 0xFFFFFFFF : 0xFFA1A1AA);
            chitra_set_draw_order(s_h_nav_text[i], 25);
        }
    }

    /* 5. Top Bar */
    s_h_topbar = chitra_create(CHITRA_PIPELINE_SHAPE);
    chitra_rect_set_bounds(s_h_topbar, 260.0f, 0.0f, 1020.0f, 64.0f);
    chitra_rect_set_border(s_h_topbar, 1.0f);
    chitra_rect_set_color(s_h_topbar, 0xFF18181B, 0xFF27272A);
    chitra_set_draw_order(s_h_topbar, 10);

    if (s_font != -1) {
        s_h_topbar_title = chitra_create(CHITRA_PIPELINE_TEXT);
        chitra_text_set(s_h_topbar_title, s_font, 20.0f, "Unified Texture Pipeline: Mode 1, Mode 2 & Mode 3", 280.0f, 20.0f);
        chitra_text_set_color(s_h_topbar_title, 0xFFE4E4E7);
        chitra_set_draw_order(s_h_topbar_title, 20);
    }

    /* =====================================================================
     * MODE 1 TEST: Standalone Large Texture (> 512x512)
     * Automatically gets its own GPU texture slot via Rule 1!
     * ===================================================================== */
    {
        int w = 600, h = 600;
        uint32_t* banner_px = (uint32_t*)malloc(w * h * sizeof(uint32_t));
        if (banner_px) {
            generate_test_pattern(banner_px, w, h, 20, 80, 160);
            s_h_standalone_banner = chitra_create(CHITRA_PIPELINE_TEXTURE);
            chitra_texture_load(s_h_standalone_banner, w, h, banner_px);
            chitra_texture_set_bounds(s_h_standalone_banner, 280.0f, 80.0f, 260.0f, 180.0f);
            chitra_set_draw_order(s_h_standalone_banner, 20);
            free(banner_px);
            printf("[Sandbox:Mode 1] Loaded 600x600 Standalone Texture successfully.\n");
        }
    }

    /* =====================================================================
     * MODE 2 TEST: Spritesheet & Local Rect Mapping (chitra_texture_set_src_rect)
     * We load a 128x64 image with two 64x64 icon frames side-by-side.
     * ===================================================================== */
    {
        int sw = 128, sh = 64;
        uint32_t* sheet_px = (uint32_t*)malloc(sw * sh * sizeof(uint32_t));
        if (sheet_px) {
            /* Left tile: Red pattern; Right tile: Green pattern */
            for (int y = 0; y < sh; y++) {
                for (int x = 0; x < 64; x++) {
                    sheet_px[y * sw + x] = 0xFF2020E0; /* Red */
                }
                for (int x = 64; x < sw; x++) {
                    sheet_px[y * sw + x] = 0xFF20E020; /* Green */
                }
            }

            /* Icon 1: Left 64x64 slice */
            s_h_spritesheet_icon1 = chitra_create(CHITRA_PIPELINE_TEXTURE);
            chitra_texture_load(s_h_spritesheet_icon1, sw, sh, sheet_px);
            chitra_texture_set_src_rect(s_h_spritesheet_icon1, 0.0f, 0.0f, 64.0f, 64.0f, 128.0f, 64.0f);
            chitra_texture_set_bounds(s_h_spritesheet_icon1, 560.0f, 80.0f, 40.0f, 40.0f);
            chitra_set_draw_order(s_h_spritesheet_icon1, 25);

            /* Icon 2: Right 64x64 slice */
            s_h_spritesheet_icon2 = chitra_create(CHITRA_PIPELINE_TEXTURE);
            chitra_texture_load(s_h_spritesheet_icon2, sw, sh, sheet_px);
            chitra_texture_set_src_rect(s_h_spritesheet_icon2, 64.0f, 0.0f, 64.0f, 64.0f, 128.0f, 64.0f);
            chitra_texture_set_bounds(s_h_spritesheet_icon2, 615.0f, 80.0f, 40.0f, 40.0f);
            chitra_set_draw_order(s_h_spritesheet_icon2, 25);

            free(sheet_px);
            printf("[Sandbox:Mode 2] Sliced 128x64 Spritesheet into two local sub-rects successfully.\n");
        }
    }

    /* =====================================================================
     * MODE 3 TEST: Dynamic Atlas Automatic Packing (<= 512x512)
     * 12 distinct cards, each with an icon packed into the shared 1024x1024 direct-to-VRAM atlas!
     * ===================================================================== */
    for (int i = 0; i < MAX_CARDS; i++) {
        s_h_card_bg[i] = chitra_create(CHITRA_PIPELINE_SHAPE);
        chitra_rect_set_radius(s_h_card_bg[i], 10.0f);
        chitra_rect_set_border(s_h_card_bg[i], 1.0f);
        chitra_rect_set_color(s_h_card_bg[i], 0xFF202023, 0xFF35353A);
        chitra_set_draw_order(s_h_card_bg[i], 30);

        /* Distinct non-uniform icon sizes to test arbitrary aspect ratio packing */
        int icon_w = 32 + (i % 3) * 16; /* 32, 48, 64 */
        int icon_h = 48 - (i % 2) * 16; /* 48, 32 */

        uint32_t* icon_px = (uint32_t*)malloc(icon_w * icon_h * sizeof(uint32_t));
        if (icon_px) {
            uint8_t r = (uint8_t)(40 + i * 15);
            uint8_t g = (uint8_t)(100 + i * 10);
            uint8_t b = (uint8_t)(180 - i * 8);
            generate_test_pattern(icon_px, icon_w, icon_h, r, g, b);

            s_h_dynamic_icons[i] = chitra_create(CHITRA_PIPELINE_TEXTURE);
            chitra_texture_load(s_h_dynamic_icons[i], icon_w, icon_h, icon_px);
            chitra_set_draw_order(s_h_dynamic_icons[i], 35);
            free(icon_px);
        }

        if (s_font != -1) {
            char tbuf[32];
            snprintf(tbuf, sizeof(tbuf), "Packed Icon %d", i + 1);
            s_h_card_title[i] = chitra_create(CHITRA_PIPELINE_TEXT);
            chitra_text_set(s_h_card_title[i], s_font, 15.0f, tbuf, 0.0f, 0.0f);
            chitra_text_set_color(s_h_card_title[i], 0xFFFFFFFF);
            chitra_set_draw_order(s_h_card_title[i], 40);

            s_h_card_desc[i] = chitra_create(CHITRA_PIPELINE_TEXT);
            chitra_text_set(s_h_card_desc[i], s_font, 12.0f, "1 Draw Call (Shared Atlas)", 0.0f, 0.0f);
            chitra_text_set_color(s_h_card_desc[i], 0xFFA1A1AA);
            chitra_set_draw_order(s_h_card_desc[i], 40);
        }
    }
    printf("[Sandbox:Mode 3] Packed %d dynamic icons into Chitra 1024x1024 Direct-to-VRAM Atlas (0B CPU RAM).\n", MAX_CARDS);

    s_last_w = 1280;
    s_last_h = 840;
    relayout(s_last_w, s_last_h);
}

/* =========================================================================
 * Layout Recalculation (In-Place Updates when Window Resizes)
 * ========================================================================= */

static void relayout(int w, int h) {
    float sidebar_w = 260.0f;

    chitra_rect_set_bounds(s_h_bg, 0.0f, 0.0f, (float)w, (float)h);
    chitra_rect_set_bounds(s_h_sidebar, 0.0f, 0.0f, sidebar_w, (float)h);
    chitra_rect_set_bounds(s_h_topbar, sidebar_w, 0.0f, (float)(w - sidebar_w), 64.0f);

    /* Layout Cards Grid below banner */
    float content_x = sidebar_w + 30.0f;
    float content_y = 280.0f;
    float card_w = 210.0f;
    float card_h = 110.0f;
    float gap = 20.0f;

    int cols = (int)((w - content_x) / (card_w + gap));
    if (cols < 1) cols = 1;

    for (int i = 0; i < MAX_CARDS; i++) {
        int row = i / cols;
        int col = i % cols;

        float cx = content_x + (float)col * (card_w + gap);
        float cy = content_y + (float)row * (card_h + gap);

        chitra_rect_set_bounds(s_h_card_bg[i], cx, cy, card_w, card_h);

        if (s_h_dynamic_icons[i] != CHITRA_INVALID_HANDLE) {
            chitra_texture_set_bounds(s_h_dynamic_icons[i], cx + 14.0f, cy + 24.0f, 48.0f, 48.0f);
        }

        if (s_font != -1) {
            chitra_text_set_pos(s_h_card_title[i], cx + 72.0f, cy + 28.0f);
            chitra_text_set_pos(s_h_card_desc[i], cx + 72.0f, cy + 54.0f);
        }
    }

    chitra_mark_dirty();
}

static void on_frame(void) {
    /* 0% CPU idle when unchanged */
}

static void on_event(const Chitra_InputEvent* ev) {
    if (ev->type == CHITRA_EVENT_RESIZED) {
        if (ev->window_width != s_last_w || ev->window_height != s_last_h) {
            s_last_w = ev->window_width;
            s_last_h = ev->window_height;
            relayout(s_last_w, s_last_h);
        }
    } else if (ev->type == CHITRA_EVENT_KEY_DOWN) {
        /* Press SPACE to toggle/destroy icons and test Atlas Defrag/Compaction */
        if (ev->key_code == 32 /* Space */) {
            s_icons_active = !s_icons_active;
            printf("[Sandbox:Compaction] Space pressed: %s icons (testing dead area & compaction)\n", 
                   s_icons_active ? "Restoring" : "Destroying half");

            if (!s_icons_active) {
                /* Destroy half the dynamic icons to generate dead space in the atlas */
                for (int i = 0; i < MAX_CARDS; i += 2) {
                    if (s_h_dynamic_icons[i] != CHITRA_INVALID_HANDLE) {
                        chitra_destroy(s_h_dynamic_icons[i]);
                        s_h_dynamic_icons[i] = CHITRA_INVALID_HANDLE;
                    }
                }
            } else {
                /* Re-create and load them back -> will trigger transparent compaction if dead space >= 30%! */
                for (int i = 0; i < MAX_CARDS; i += 2) {
                    if (s_h_dynamic_icons[i] == CHITRA_INVALID_HANDLE) {
                        int icon_w = 48, icon_h = 48;
                        uint32_t px[48 * 48];
                        generate_test_pattern(px, icon_w, icon_h, 200, 150, 50);

                        s_h_dynamic_icons[i] = chitra_create(CHITRA_PIPELINE_TEXTURE);
                        chitra_texture_load(s_h_dynamic_icons[i], icon_w, icon_h, px);
                        chitra_set_draw_order(s_h_dynamic_icons[i], 35);
                    }
                }
                relayout(s_last_w, s_last_h);
            }
            chitra_mark_dirty();
        }
    }
}

/* =========================================================================
 * Main Entry Point
 * ========================================================================= */

int main(void) {
    Chitra_Config config = {
        .width      = 1280,
        .height     = 840,
        .title      = "Chitra V1 - Unified Texture Pipeline Sandbox",
        .target_fps = 0, /* VSync */
        .on_init    = on_init,
        .on_frame   = on_frame,
        .on_event   = on_event
    };

    printf("[Sandbox] Launching Chitra standalone runner...\n");
    chitra_run(&config);
    return 0;
}
