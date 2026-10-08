#include "chitra.h"
#include "chitra_hal.h"
#include "chitra_rhi.h"
#include <stdio.h>

static const Chitra_HAL_API* g_hal = NULL;
static const Chitra_RHI_API* g_rhi = NULL;

void chitra_run(const Chitra_Config* config) {
    if (!config) return;
    
    printf("[Chitra] Core Initializing: %dx%d '%s'\n", config->width, config->height, config->title);
    
    /* 1. Bind Backends (Implemented by linked backend files) */
    g_hal = chitra_hal_get_api();
    g_rhi = chitra_rhi_get_api();
    
    if (!g_hal || !g_rhi) {
        printf("[Chitra] Error: Failed to load backends.\n");
        return;
    }
    
    /* 2. Run the HAL Application Loop */
    g_hal->init(config);
    g_hal->run();
}
