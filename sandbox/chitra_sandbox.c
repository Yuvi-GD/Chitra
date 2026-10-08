#include "chitra.h"
#include <stdio.h>

int main() {
    Chitra_Config config = {
        .width = 800,
        .height = 600,
        .title = "Chitra Standalone Renderer"
    };
    
    printf("Starting Chitra Sandbox...\n");
    chitra_run(&config);
    return 0;
}
