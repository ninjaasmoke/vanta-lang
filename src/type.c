#include "type.h"
#include <stdio.h>

int type_is_integer(const Type *t) {
    if (!t) return 0;
    switch (t->kind) {
    case TY_INT: case TY_I32: case TY_I64:
    case TY_U32: case TY_U64: case TY_U8: return 1;
    default: return 0;
    }
}

int type_is_numeric(const Type *t) {
    if (!t) return 0;
    if (type_is_integer(t)) return 1;
    return t->kind == TY_F32 || t->kind == TY_F64;
}

int type_equals(const Type *a, const Type *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    /* TY_ERROR is the wildcard: equal to anything to suppress cascade */
    if (a->kind == TY_ERROR || b->kind == TY_ERROR) return 1;
    if (a->kind != b->kind) return 0;
    switch (a->kind) {
    case TY_PTR:
        /* *void on either side is the null literal — compatible with any *T */
        if (a->elem && a->elem->kind == TY_VOID) return 1;
        if (b->elem && b->elem->kind == TY_VOID) return 1;
        return type_equals(a->elem, b->elem);
    case TY_SLICE: return type_equals(a->elem, b->elem);
    case TY_ARRAY: return a->array_len == b->array_len && type_equals(a->elem, b->elem);
    case TY_STRUCT: return a == b;  /* nominal */
    default: return 1;              /* same primitive kind */
    }
}

const char *type_name(const Type *t) {
    if (!t) return "<null>";
    switch (t->kind) {
    case TY_VOID: return "void";
    case TY_BOOL: return "bool";
    case TY_INT:  return "int";
    case TY_I32:  return "i32";
    case TY_I64:  return "i64";
    case TY_U32:  return "u32";
    case TY_U64:  return "u64";
    case TY_U8:   return "u8";
    case TY_F32:  return "f32";
    case TY_F64:  return "f64";
    case TY_PTR:    return "*T";
    case TY_SLICE:  return "[]T";
    case TY_ARRAY:  return "[N]T";
    case TY_STRUCT: return t->struct_name ? t->struct_name : "<anon-struct>";
    case TY_ERROR:  return "<error>";
    }
    return "?";
}
