#include "chitra.h"
#include <stdio.h>
#include <string.h>

#define MAX_CARDS 20
#define MAX_NAV_ITEMS 12

static int s_font = -1;
static int s_last_w = 0;
static int s_last_h = 0;

static void relayout(int w, int h);

/* Retained Element Handles (Created once, updated in-place) */
static uint32_t s_h_bg = CHITRA_INVALID_HANDLE;
static uint32_t s_h_sidebar = CHITRA_INVALID_HANDLE;
static uint32_t s_h_logo_text = CHITRA_INVALID_HANDLE;
static uint32_t s_h_topbar = CHITRA_INVALID_HANDLE;
static uint32_t s_h_topbar_title = CHITRA_INVALID_HANDLE;
static uint16_t s_test_tex = 0;
static uint32_t s_h_tex_icon = CHITRA_INVALID_HANDLE;

static uint32_t s_h_nav_bg[MAX_NAV_ITEMS];
static uint32_t s_h_nav_text[MAX_NAV_ITEMS];

static uint32_t s_h_card_bg[MAX_CARDS];
static uint32_t s_h_card_banner[MAX_CARDS];
static uint32_t s_h_card_title[MAX_CARDS];
static uint32_t s_h_card_desc1[MAX_CARDS];
static uint32_t s_h_card_desc2[MAX_CARDS];

static int s_hovered_nav = -1;

/* =========================================================================
 * Application Initialization (Create Retained Elements Once)
 * ========================================================================= */

static void on_init(void) {
    printf("[Sandbox] Initializing Retained UI Scene...\n");

    s_font = chitra_load_font("sans", "../../sandbox/Font/Roboto-Regular.ttf");
    if (s_font == -1) {
        printf("[Sandbox] Warning: Failed to load font, attempting fallback relative path...\n");
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
        chitra_text_set(s_h_logo_text, s_font, 26.0f, "Chitra Engine", 30.0f, 28.0f);
        chitra_text_set_color(s_h_logo_text, 0xFFE4E4E7);
        chitra_set_draw_order(s_h_logo_text, 20);
    }

    /* 4. Sidebar Navigation Items */
    for (int i = 0; i < MAX_NAV_ITEMS; i++) {
        float y = 80.0f + (float)i * 44.0f;
        uint32_t bg_col = (i == 1) ? 0xFF27272A : 0xFF18181B;

        s_h_nav_bg[i] = chitra_create(CHITRA_PIPELINE_SHAPE);
        chitra_rect_set_bounds(s_h_nav_bg[i], 20.0f, y, 220.0f, 36.0f);
        chitra_rect_set_radius(s_h_nav_bg[i], 8.0f);
        chitra_rect_set_color(s_h_nav_bg[i], bg_col, 0);
        chitra_set_draw_order(s_h_nav_bg[i], 15);

        if (s_font != -1) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Navigation %d", i + 1);
            s_h_nav_text[i] = chitra_create(CHITRA_PIPELINE_TEXT);
            chitra_text_set(s_h_nav_text[i], s_font, 15.0f, buf, 36.0f, y + 9.0f);
            chitra_text_set_color(s_h_nav_text[i], 0xFFA1A1AA);
            chitra_set_draw_order(s_h_nav_text[i], 25);
        }
    }

    /* 5. Top Bar */
    s_h_topbar = chitra_create(CHITRA_PIPELINE_SHAPE);
    chitra_rect_set_bounds(s_h_topbar, 260.0f, 0.0f, 1020.0f, 70.0f);
    chitra_rect_set_border(s_h_topbar, 1.0f);
    chitra_rect_set_color(s_h_topbar, 0xFF18181B, 0xFF27272A);
    chitra_set_draw_order(s_h_topbar, 10);

    if (s_font != -1) {
        s_h_topbar_title = chitra_create(CHITRA_PIPELINE_TEXT);
        chitra_text_set(s_h_topbar_title, s_font, 24.0f, "Dashboard View", 290.0f, 24.0f);
        chitra_text_set_color(s_h_topbar_title, 0xFFE4E4E7);
        chitra_set_draw_order(s_h_topbar_title, 20);
    }

    /* 5b. Texture Avatar / Icon Widget */
    uint32_t pixels[64 * 64];
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            uint8_t r = (uint8_t)(x * 4);
            uint8_t g = (uint8_t)(y * 4);
            uint8_t b = 220;
            uint8_t a = 255;
            pixels[y * 64 + x] = (a << 24) | (b << 16) | (g << 8) | r;
        }
    }
    s_test_tex = chitra_create_texture_rgba(64, 64);
    chitra_update_texture_rgba(s_test_tex, pixels, 64, 64);

    s_h_tex_icon = chitra_create(CHITRA_PIPELINE_TEXTURE);
    chitra_set_texture(s_h_tex_icon, s_test_tex);
    chitra_texture_set_bounds(s_h_tex_icon, 1220.0f, 15.0f, 40.0f, 40.0f);
    chitra_set_draw_order(s_h_tex_icon, 25);

    /* 6. Main Content Cards */
    for (int i = 0; i < MAX_CARDS; i++) {
        s_h_card_bg[i] = chitra_create(CHITRA_PIPELINE_SHAPE);
        chitra_rect_set_radius(s_h_card_bg[i], 14.0f);
        chitra_rect_set_border(s_h_card_bg[i], 1.0f);
        chitra_rect_set_color(s_h_card_bg[i], 0xFF202023, 0xFF35353A);
        chitra_set_draw_order(s_h_card_bg[i], 30);

        s_h_card_banner[i] = chitra_create(CHITRA_PIPELINE_SHAPE);
        chitra_rect_set_radius(s_h_card_banner[i], 8.0f);
        chitra_rect_set_color(s_h_card_banner[i], 0xFFCE6A38, 0);
        chitra_set_draw_order(s_h_card_banner[i], 35);

        if (s_font != -1) {
            char title_buf[32];
            snprintf(title_buf, sizeof(title_buf), "Card Widget %d", i + 1);

            s_h_card_title[i] = chitra_create(CHITRA_PIPELINE_TEXT);
            chitra_text_set(s_h_card_title[i], s_font, 16.0f, title_buf, 0.0f, 0.0f);
            chitra_text_set_color(s_h_card_title[i], 0xFFFFFFFF);
            chitra_set_draw_order(s_h_card_title[i], 40);

            s_h_card_desc1[i] = chitra_create(CHITRA_PIPELINE_TEXT);
            chitra_text_set(s_h_card_desc1[i], s_font, 13.0f, "Dynamic 2-Table RAM", 0.0f, 0.0f);
            chitra_text_set_color(s_h_card_desc1[i], 0xFFA1A1AA);
            chitra_set_draw_order(s_h_card_desc1[i], 40);

            s_h_card_desc2[i] = chitra_create(CHITRA_PIPELINE_TEXT);
            chitra_text_set(s_h_card_desc2[i], s_font, 12.0f, "Hole-skipping 32b index", 0.0f, 0.0f);
            chitra_text_set_color(s_h_card_desc2[i], 0xFF71717A);
            chitra_set_draw_order(s_h_card_desc2[i], 40);
        }
    }

    s_last_w = 1280;
    s_last_h = 840;
    relayout(s_last_w, s_last_h);
}

/* =========================================================================
 * Layout Recalculation (In-Place Updates when Window Resizes)
 * ========================================================================= */

static void relayout(int w, int h) {
    float sidebar_w = 260.0f;

    /* Update Background */
    chitra_rect_set_bounds(s_h_bg, 0.0f, 0.0f, (float)w, (float)h);

    /* Update Sidebar */
    chitra_rect_set_bounds(s_h_sidebar, 0.0f, 0.0f, sidebar_w, (float)h);

    /* Update Topbar */
    chitra_rect_set_bounds(s_h_topbar, sidebar_w, 0.0f, (float)(w - sidebar_w), 70.0f);

    /* Update Texture Widget */
    if (s_h_tex_icon != CHITRA_INVALID_HANDLE) {
        chitra_texture_set_bounds(s_h_tex_icon, (float)(w - 60), 15.0f, 40.0f, 40.0f);
    }

    /* Layout Cards Grid */
    float content_x = sidebar_w + 30.0f;
    float content_y = 90.0f;
    float card_w = 200.0f;
    float card_h = 200.0f;
    float gap = 20.0f;

    int cols = (int)((w - content_x) / (card_w + gap));
    if (cols < 1) cols = 1;

    for (int i = 0; i < MAX_CARDS; i++) {
        int row = i / cols;
        int col = i % cols;

        float cx = content_x + (float)col * (card_w + gap);
        float cy = content_y + (float)row * (card_h + gap);

        chitra_rect_set_bounds(s_h_card_bg[i], cx, cy, card_w, card_h);
        chitra_rect_set_bounds(s_h_card_banner[i], cx + 12.0f, cy + 12.0f, card_w - 24.0f, 90.0f);

        /* In-place vertex translation: Zero font layout or string re-parsing cost! */
        if (s_font != -1) {
            chitra_text_set_pos(s_h_card_title[i], cx + 16.0f, cy + 120.0f);
            chitra_text_set_pos(s_h_card_desc1[i], cx + 16.0f, cy + 145.0f);
            chitra_text_set_pos(s_h_card_desc2[i], cx + 16.0f, cy + 165.0f);
        }
    }

    chitra_mark_dirty();
}

static void on_frame(void) {
    /* If window size is unchanged, zero work is performed on CPU!
     * The engine sits in 0.0% idle wait. */
}

static void on_event(const Chitra_InputEvent* ev) {
    if (ev->type == CHITRA_EVENT_RESIZED) {
        if (ev->window_width != s_last_w || ev->window_height != s_last_h) {
            s_last_w = ev->window_width;
            s_last_h = ev->window_height;
            relayout(s_last_w, s_last_h);
        }
    } else if (ev->type == CHITRA_EVENT_MOUSE_MOVE) {
        /* Check hover over navigation items */
        int hovered_nav = -1;
        if (ev->mouse_x >= 20.0f && ev->mouse_x <= 240.0f) {
            for (int i = 0; i < MAX_NAV_ITEMS; i++) {
                float y = 80.0f + (float)i * 44.0f;
                if (ev->mouse_y >= y && ev->mouse_y <= y + 36.0f) {
                    hovered_nav = i;
                    break;
                }
            }
        }

        if (hovered_nav != s_hovered_nav) {
            /* Restore old nav item */
            if (s_hovered_nav != -1) {
                chitra_rect_set_fill_color(s_h_nav_bg[s_hovered_nav], 0xFF18181B);
                if (s_font != -1) chitra_text_set_color(s_h_nav_text[s_hovered_nav], 0xFFA1A1AA);
            }
            /* Highlight new nav item */
            if (hovered_nav != -1) {
                chitra_rect_set_fill_color(s_h_nav_bg[hovered_nav], 0xFF2F2F35);
                if (s_font != -1) chitra_text_set_color(s_h_nav_text[hovered_nav], 0xFFFFFFFF);
            }
            s_hovered_nav = hovered_nav;
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
        .title      = "Chitra Engine - Retained Mode Sandbox",
        .target_fps = 0, /* 0 = VSync (0% CPU/GPU idle sleep enabled) */
        .on_init    = on_init,
        .on_frame   = on_frame,
        .on_event   = on_event
    };

    printf("[Sandbox] Launching Chitra standalone runner...\n");
    chitra_run(&config);
    return 0;
}
