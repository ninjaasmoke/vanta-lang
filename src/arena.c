#include "arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct ArenaBlock {
    ArenaBlock *next;
    size_t      cap;
    size_t      used;
    /* memory follows the header */
};

static ArenaBlock *new_block(size_t bytes) {
    ArenaBlock *b = (ArenaBlock *)malloc(sizeof(ArenaBlock) + bytes);
    if (!b) {
        fputs("arena: out of memory\n", stderr);
        abort();
    }
    b->next = NULL;
    b->cap  = bytes;
    b->used = 0;
    return b;
}

void arena_init(Arena *a, size_t block_size) {
    if (block_size < 4096) block_size = 4096;
    a->block_size = block_size;
    a->head = NULL;
}

static size_t align_up(size_t n, size_t a) {
    return (n + (a - 1)) & ~(a - 1);
}

void *arena_alloc(Arena *a, size_t bytes) {
    bytes = align_up(bytes, sizeof(void *));
    if (!a->head || a->head->used + bytes > a->head->cap) {
        size_t cap = bytes > a->block_size ? bytes : a->block_size;
        ArenaBlock *b = new_block(cap);
        b->next = a->head;
        a->head = b;
    }
    char *base = (char *)(a->head + 1);
    void *p = base + a->head->used;
    a->head->used += bytes;
    return p;
}

void *arena_alloc_zero(Arena *a, size_t bytes) {
    void *p = arena_alloc(a, bytes);
    memset(p, 0, bytes);
    return p;
}

char *arena_strdup(Arena *a, const char *s) {
    return arena_strndup(a, s, strlen(s));
}

char *arena_strndup(Arena *a, const char *s, size_t n) {
    char *p = (char *)arena_alloc(a, n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

void arena_free(Arena *a) {
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
}
