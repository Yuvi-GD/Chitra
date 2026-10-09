#include "chitra.h"
#include <stdio.h>
#include "sokol_app.h"

static int s_last_w = 0;
static int s_last_h = 0;

static int s_font = -1;

#include <string.h>

static void on_init(void) {
    printf("Sandbox Init\n");
    
    s_font = chitra_load_font("sans", "../../sandbox/Font/Roboto-Regular.ttf");
    
    if (s_font == -1) {
        printf("Failed to load font!\n");
    }
}

static void on_frame(void) {
    int w = sapp_width();
    int h = sapp_height();
    
    bool dirty = false;
    if (w != s_last_w || h != s_last_h) {
        dirty = true;
        s_last_w = w;
        s_last_h = h;
    }
    
    if (!dirty) {
        return;
    }
    
    chitra_cmd_clear();
    
    /* Background */
    chitra_draw_rect(0, 0, 0, (float)w, (float)h, 0.0f, 0.0f, 0xFF0F0F0F, 0);
    
    /* Sidebar */
    float sidebar_w = 260.0f;
    chitra_draw_rect(1, 0, 0, sidebar_w, (float)h, 0.0f, 0.0f, 0xFF18181B, 0);
    
    /* Sidebar Logo Text */
    if (s_font != -1) {
        chitra_draw_text(3, s_font, 28.0f, 30.0f, 30.0f, "Chitra Engine", 0xFFE4E4E7);
    }
    
    /* Sidebar Items */
    for (int i = 0; i < 12; i++) {
        float y = 90.0f + i * 45.0f;
        uint32_t bg_color = (i == 1) ? 0xFF27272A : 0xFF18181B; /* Highlight 2nd item */
        uint32_t text_color = (i == 1) ? 0xFFFFFFFF : 0xFFA1A1AA;
        
        chitra_draw_rect(2, 20.0f, y, sidebar_w - 40.0f, 36.0f, 8.0f, 0.0f, bg_color, 0);
        if (s_font != -1) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Navigation %d", i + 1);
            chitra_draw_text(3, s_font, 16.0f, 36.0f, y + 10.0f, buf, text_color);
        }
    }
    
    /* Top Bar */
    chitra_draw_rect(1, sidebar_w, 0, w - sidebar_w, 70.0f, 0.0f, 1.0f, 0xFF18181B, 0xFF27272A);
    if (s_font != -1) {
        chitra_draw_text(3, s_font, 28.0f, sidebar_w + 30.0f, 28.0f, "Dashboard View", 0xFFE4E4E7);
    }
    
    /* Main Content Area Grid */
    float content_x = sidebar_w + 40.0f;
    float content_y = 100.0f;
    float card_size = 220.0f;
    float gap = 24.0f;
    
    int cols = (int)((w - content_x) / (card_size + gap));
    if (cols < 1) cols = 1;
    
    for (int i = 0; i < 20; i++) {
        int row = i / cols;
        int col = i % cols;
        
        float x = content_x + col * (card_size + gap);
        float y = content_y + row * (card_size + gap);
        
        /* Card Shadow/Border */
        chitra_draw_rect(2, x, y, card_size, card_size, 16.0f, 1.0f, 0xFF27272A, 0xFF3F3F46);
        
        /* Card Inner Image Element */
        uint32_t img_color = 0xFFFF7733; /* Vibrant blue/cyan in ABGR (0xFF BB GG RR -> 0xFF 33 77 FF) wait: 0xFFD8783B */
        chitra_draw_rect(3, x + 16, y + 16, card_size - 32, 110.0f, 10.0f, 0.0f, 0xFFCE6A38, 0);
        
        if (s_font != -1) {
            chitra_draw_text(4, s_font, 22.0f, x + 30, y + 80, "Modern UI", 0xFFFFFFFF);
            
            char buf[32];
            snprintf(buf, sizeof(buf), "Component %d", i + 1);
            chitra_draw_text(3, s_font, 18.0f, x + 20, y + 160.0f, buf, 0xFFE4E4E7);
            chitra_draw_text(3, s_font, 13.0f, x + 20, y + 185.0f, "Retained mode rendering", 0xFFA1A1AA);
            chitra_draw_text(3, s_font, 13.0f, x + 20, y + 203.0f, "Zero allocation pipeline", 0xFFA1A1AA);
        } else {
            /* Card Text Placeholder */
            chitra_draw_rect(3, x + 20, y + card_size - 60, card_size - 80, 15.0f, 4.0f, 0.0f, 0xFF666666, 0);
            chitra_draw_rect(3, x + 20, y + card_size - 30, card_size - 100, 15.0f, 4.0f, 0.0f, 0xFF555555, 0);
        }
    }
    
}

int main(void) {
    Chitra_Config config = {
        .width = 1280,
        .height = 840,
        .title = "Chitra - Complex Sandbox",
        .target_fps = 0, /* 0 = Default VSync. You can change this to 60, 144, 240, etc. */
        .on_init = on_init,
        .on_frame = on_frame
    };
    
    printf("Starting Chitra Sandbox...\n");
    chitra_run(&config);
    return 0;
}
