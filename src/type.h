/*
 * type.h - resolved types for the type checker / interpreter.
 *
 * AST has AstType (syntactic). Sema produces Type (semantic), which
 * has identity (pointers compare). Each Type is interned in a Sema
 * context so `int* == int*`.
 */

#ifndef VANTA_TYPE_H
#define VANTA_TYPE_H

#include <stddef.h>
#include "vec.h"

typedef struct Type Type;

typedef enum {
    TY_VOID, TY_BOOL,
    TY_INT,     /* default int (==i64 on this impl) */
    TY_I32, TY_I64, TY_U32, TY_U64,
    TY_U8,      /* one byte; what 'c' character literals produce */
    TY_F32, TY_F64,
    TY_PTR,
    TY_ARRAY,
    TY_SLICE,
    TY_STRUCT,
    TY_ERROR     /* sentinel after a sema error; suppresses cascade */
} TypeKind;

typedef struct {
    const char *name;
    Type       *type;
    size_t      offset;   /* byte offset within struct */
} StructFieldInfo;

typedef VEC(StructFieldInfo) StructFieldInfoVec;

struct Type {
    TypeKind kind;
    /* TY_PTR / TY_ARRAY / TY_SLICE */
    Type *elem;
    /* TY_ARRAY */
    long long array_len;
    /* TY_STRUCT */
    const char        *struct_name;
    StructFieldInfoVec fields;
    size_t             size;     /* in bytes, for codegen/interp */
};

int   type_is_integer(const Type *t);
int   type_is_numeric(const Type *t);
int   type_equals(const Type *a, const Type *b);
const char *type_name(const Type *t);

#endif
