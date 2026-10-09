#ifndef CHITRA_ARENA_H
#define CHITRA_ARENA_H

#include <stdint.h>
#include <stddef.h>

/* 
 * Dynamic Memory Arena
 * Used for frame-lifetime allocations like Vertex Buffers and Command Queues.
 * Replaces the 10MB static arrays from the old architecture to ensure sub-1MB RAM usage.
 */
typedef struct {
    uint8_t* base;
    size_t size;
    size_t capacity;
} Chitra_Arena;

/* Initializes the arena with a starting capacity (e.g., 64KB) */
void chitra_arena_init(Chitra_Arena* arena, size_t initial_capacity);

/* Allocates 'size' bytes from the arena. Grows automatically if out of space. */
void* chitra_arena_push(Chitra_Arena* arena, size_t size);

/* Resets the allocation pointer to 0 without freeing the underlying memory. */
void chitra_arena_clear(Chitra_Arena* arena);

/* Frees the underlying memory completely. */
void chitra_arena_term(Chitra_Arena* arena);

#endif // CHITRA_ARENA_H
