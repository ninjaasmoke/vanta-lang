/*
 * vec.h — generic dynamic array via macro.
 *
 * Usage:
 *   typedef VEC(int) IntVec;
 *   IntVec v = {0};
 *   vec_push(&v, 42);
 *   ... v.data[i], v.len ...
 *   vec_free(&v);
 */

#ifndef VANTA_VEC_H
#define VANTA_VEC_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define VEC(T) struct { T *data; size_t len; size_t cap; }

#define vec_reserve(v, n) do { \
    if ((n) > (v)->cap) { \
        size_t _nc = (v)->cap ? (v)->cap : 8; \
        while (_nc < (n)) _nc *= 2; \
        (v)->data = realloc((v)->data, _nc * sizeof(*(v)->data)); \
        (v)->cap = _nc; \
    } \
} while (0)

#define vec_push(v, x) do { \
    vec_reserve((v), (v)->len + 1); \
    (v)->data[(v)->len++] = (x); \
} while (0)

#define vec_free(v) do { \
    free((v)->data); \
    (v)->data = NULL; \
    (v)->len = (v)->cap = 0; \
} while (0)

#define vec_last(v) ((v)->data[(v)->len - 1])

#endif
