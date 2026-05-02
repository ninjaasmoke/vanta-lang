/*
 * arena.h - bump allocator.
 *
 * Compiler passes allocate a lot of small nodes. An arena is the
 * easiest way to manage their lifetime: free everything at the end.
 */

#ifndef VANTA_ARENA_H
#define VANTA_ARENA_H

#include <stddef.h>

typedef struct ArenaBlock ArenaBlock;

typedef struct {
    ArenaBlock *head;
    size_t      block_size;
} Arena;

void  arena_init(Arena *a, size_t block_size);
void *arena_alloc(Arena *a, size_t bytes);
void *arena_alloc_zero(Arena *a, size_t bytes);
char *arena_strdup(Arena *a, const char *s);
char *arena_strndup(Arena *a, const char *s, size_t n);
void  arena_free(Arena *a);

#endif
