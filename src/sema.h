/*
 * sema.h — semantic analysis (name + type resolution).
 *
 * Walks the AST, resolves identifiers, type-checks expressions/stmts,
 * and decorates function variants with their attribute set so the
 * variant resolver can pick one at the call site.
 *
 * Sema is destructive on the parsed program in the sense that it adds
 * type annotations as a side-table keyed by Expr*.
 */

#ifndef VANTA_SEMA_H
#define VANTA_SEMA_H

#include "ast.h"
#include "type.h"
#include "arena.h"

typedef struct Sema Sema;

/* attribute set: a small list of interned names. */
typedef struct {
    const char **names;   /* arena-allocated array of interned strings */
    size_t       count;
} AttrSet;

/* a function variant — one Decl + its required attribute set */
typedef struct {
    Decl    *decl;
    AttrSet  required;       /* attrs that must be active for this variant */
    /* parameter / return types (resolved) */
    Type   **param_types;
    size_t   param_count;
    Type    *ret_type;
} FnVariant;

typedef struct {
    const char *name;
    /* slice of variants, all sharing the same name */
    FnVariant *items;
    size_t     count;
} FnVariantSet;

/* the program after sema */
typedef struct {
    Module       *module;
    FnVariantSet *fns;        /* one per unique fn name */
    size_t        fn_count;
    Type        **struct_types;
    size_t        struct_count;
    AttrSet       active;     /* the attrs the user passed via --attr */
    int           had_error;
} SemaProgram;

SemaProgram *sema_analyze(Arena *arena, Module *m, const AttrSet *active);

/* Look up the type sema attached to an expression (or NULL). */
Type *sema_expr_type(Expr *e);

/* Helpers for the CLI (parsing --attr foo --attr bar into an AttrSet). */
AttrSet attrset_make(Arena *a, const char **names, size_t n);
int     attrset_contains(const AttrSet *s, const char *name);
int     attrset_subset(const AttrSet *small, const AttrSet *big);

#endif
