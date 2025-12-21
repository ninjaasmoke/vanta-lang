/*
 * lower.h — flatten attributes into runtime-checked invariants.
 *
 * Per the spec, `@requires` / `@ensures` / `@invariant` are gated by
 * the attribute list they appear in. After lowering, each FnVariant
 * has a tidy list of expressions to evaluate at entry / before return,
 * and each struct has a list of invariants the interpreter can check
 * after mutations or at the function boundary.
 *
 * This pass runs after sema. If gating attrs are not active, the
 * invariant is dropped here — sema_program is the only place those
 * lists exist.
 */

#ifndef VANTA_LOWER_H
#define VANTA_LOWER_H

#include "sema.h"

typedef struct {
    Expr *cond;        /* the condition expression */
    Loc   loc;
    const char *kind;  /* "requires" | "ensures" | "invariant" | "constraint" */
} Invariant;

typedef VEC(Invariant) InvariantVec;

/* Side-table keyed by FnVariant index in SemaProgram.fns flattened. */
typedef struct {
    InvariantVec  requires_;
    InvariantVec  ensures;
    /* expressions captured on entry for `old(...)` */
    ExprVec       old_exprs;
} LoweredFn;

typedef struct {
    Type         *type;          /* the struct type */
    InvariantVec  invariants;    /* active invariants for this struct */
} LoweredStruct;

typedef struct {
    SemaProgram   *prog;
    /* parallel to prog->fns: for each variant set, an array of LoweredFn */
    LoweredFn   ***fns;          /* fns[set_idx][variant_idx] */
    LoweredStruct *structs;
    size_t         struct_count;
} LoweredProgram;

LoweredProgram *lower(Arena *a, SemaProgram *prog);

/* Locate the LoweredFn for a chosen variant. */
LoweredFn *lower_find(LoweredProgram *L, FnVariant *v);

#endif
