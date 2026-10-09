#include "chitra_arena.h"
#include <stdlib.h>
#include <string.h>

void chitra_arena_init(Chitra_Arena* arena, size_t initial_capacity) {
    if (!arena) return;
    
    // Default to 64KB if 0 is passed
    if (initial_capacity == 0) initial_capacity = 65536;
    
    arena->base = (uint8_t*)malloc(initial_capacity);
    arena->size = 0;
    arena->capacity = arena->base ? initial_capacity : 0;
}

void* chitra_arena_push(Chitra_Arena* arena, size_t size) {
    if (!arena || size == 0) return NULL;
    
    if (arena->size + size > arena->capacity) {
        size_t new_cap = arena->capacity * 2;
        while (arena->size + size > new_cap) {
            new_cap *= 2;
        }
        
        uint8_t* new_base = (uint8_t*)realloc(arena->base, new_cap);
        if (!new_base) return NULL; // Out of memory
        
        arena->base = new_base;
        arena->capacity = new_cap;
    }
    
    void* ptr = arena->base + arena->size;
    arena->size += size;
    return ptr;
}

void chitra_arena_clear(Chitra_Arena* arena) {
    if (arena) arena->size = 0;
}

void chitra_arena_term(Chitra_Arena* arena) {
    if (!arena) return;
    if (arena->base) free(arena->base);
    arena->base = NULL;
    arena->size = 0;
    arena->capacity = 0;
}
