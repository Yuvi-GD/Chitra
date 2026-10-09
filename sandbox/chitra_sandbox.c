#include "chitra.h"
#include <stdio.h>
#include "sokol_app.h"

static int s_last_w = 0;
static int s_last_h = 0;

static void on_init(void) {
    printf("Sandbox Init\n");
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
    chitra_draw_rect(0, 0, 0, (float)w, (float)h, 0.0f, 0.0f, 0xFF1E1E1E, 0);
    
    /* Sidebar */
    float sidebar_w = 250.0f;
    chitra_draw_rect(1, 0, 0, sidebar_w, (float)h, 0.0f, 0.0f, 0xFF252526, 0);
    
    /* Sidebar Items */
    for (int i = 0; i < 15; i++) {
        float y = 50.0f + i * 40.0f;
        chitra_draw_rect(2, 20.0f, y, sidebar_w - 40.0f, 30.0f, 6.0f, 0.0f, 0xFF3E3E42, 0);
    }
    
    /* Top Bar */
    chitra_draw_rect(1, sidebar_w, 0, w - sidebar_w, 60.0f, 0.0f, 1.0f, 0xFF333333, 0xFF454545);
    
    /* Main Content Area Grid */
    float content_x = sidebar_w + 30.0f;
    float content_y = 90.0f;
    float card_size = 200.0f;
    float gap = 20.0f;
    
    int cols = (int)((w - content_x) / (card_size + gap));
    if (cols < 1) cols = 1;
    
    for (int i = 0; i < 20; i++) {
        int row = i / cols;
        int col = i % cols;
        
        float x = content_x + col * (card_size + gap);
        float y = content_y + row * (card_size + gap);
        
        /* Card Shadow/Border */
        chitra_draw_rect(2, x, y, card_size, card_size, 12.0f, 2.0f, 0xFF2D2D30, 0xFF555555);
        
        /* Card Inner Element */
        chitra_draw_rect(3, x + 20, y + 20, card_size - 40, card_size - 100, 8.0f, 0.0f, 0xFF007ACC, 0);
        
        /* Card Text Placeholder */
        chitra_draw_rect(3, x + 20, y + card_size - 60, card_size - 80, 15.0f, 4.0f, 0.0f, 0xFF666666, 0);
        chitra_draw_rect(3, x + 20, y + card_size - 30, card_size - 100, 15.0f, 4.0f, 0.0f, 0xFF555555, 0);
    }
    
}

int main(void) {
    Chitra_Config config = {
        .width = 1200,
        .height = 800,
        .title = "Chitra V3 - Complex Sandbox",
        .on_init = on_init,
        .on_frame = on_frame
    };
    
    printf("Starting Chitra Sandbox...\n");
    chitra_run(&config);
    return 0;
}
