#include "lower.h"

#include <stdlib.h>
#include <string.h>

/* helper: gather all `old(...)` exprs reachable from `e` */
static void collect_olds(Expr *e, ExprVec *out) {
    if (!e) return;
    switch (e->kind) {
    case EX_OLD: vec_push(out, e); collect_olds(e->old_.inner, out); break;
    case EX_UNARY:  collect_olds(e->unary.e, out); break;
    case EX_BINARY: collect_olds(e->binary.l, out); collect_olds(e->binary.r, out); break;
    case EX_ASSIGN: collect_olds(e->assign.lhs, out); collect_olds(e->assign.rhs, out); break;
    case EX_CALL:
        collect_olds(e->call.callee, out);
        for (size_t i = 0; i < e->call.args.len; i++) collect_olds(e->call.args.data[i], out);
        break;
    case EX_INDEX:  collect_olds(e->index_.base, out); collect_olds(e->index_.index, out); break;
    case EX_FIELD:  collect_olds(e->field.base, out); break;
    case EX_RANGE:  collect_olds(e->range.lo, out); collect_olds(e->range.hi, out); break;
    case EX_STRUCT_LIT:
        for (size_t i = 0; i < e->struct_lit.fields.len; i++)
            collect_olds(e->struct_lit.fields.data[i].value, out);
        break;
    default: break;
    }
}

static int is_invariant_kind(const char *n) {
    return strcmp(n, "requires") == 0 || strcmp(n, "ensures") == 0
        || strcmp(n, "invariant") == 0 || strcmp(n, "constraint") == 0;
}

static int gates_active(const AttrSet *active, const char **gates, size_t gn) {
    for (size_t i = 0; i < gn; i++)
        if (!attrset_contains(active, gates[i])) return 0;
    return 1;
}

LoweredProgram *lower(Arena *a, SemaProgram *prog) {
    LoweredProgram *L = (LoweredProgram *)arena_alloc_zero(a, sizeof(LoweredProgram));
    L->prog = prog;

    /* per-variant lowering */
    L->fns = (LoweredFn ***)arena_alloc_zero(a, sizeof(LoweredFn **) * (prog->fn_count + 1));
    for (size_t i = 0; i < prog->fn_count; i++) {
        FnVariantSet vs = prog->fns[i];
        LoweredFn **arr = (LoweredFn **)arena_alloc_zero(a, sizeof(LoweredFn *) * (vs.count + 1));
        for (size_t j = 0; j < vs.count; j++) {
            LoweredFn *lf = (LoweredFn *)arena_alloc_zero(a, sizeof(LoweredFn));
            Decl *d = vs.items[j].decl;
            /* walk attrs left-to-right; track gate prefix (non-invariant attrs).
             * the variant itself is gated by those, so it's already redundant
             * for fn invariants — but we still respect explicit gating in
             * case the user nests differently. */
            const char *gates[16]; size_t gn = 0;
            for (size_t k = 0; k < d->attrs.len; k++) {
                Attr *at = d->attrs.data[k];
                if (!is_invariant_kind(at->name)) {
                    if (gn < 16) gates[gn++] = at->name;
                    continue;
                }
                if (!gates_active(&prog->active, gates, gn)) continue;
                Invariant inv = { at->arg, at->loc, at->name };
                if (strcmp(at->name, "requires") == 0)      vec_push(&lf->requires_, inv);
                else if (strcmp(at->name, "ensures") == 0)  vec_push(&lf->ensures, inv);
                /* @invariant on a fn doesn't really make sense; ignore. */
            }
            /* gather old(...) exprs from ensures so interp can capture entry values */
            for (size_t k = 0; k < lf->ensures.len; k++)
                collect_olds(lf->ensures.data[k].cond, &lf->old_exprs);
            arr[j] = lf;
        }
        L->fns[i] = arr;
    }

    /* struct invariants */
    L->struct_count = prog->struct_count;
    L->structs = (LoweredStruct *)arena_alloc_zero(a, sizeof(LoweredStruct) * (prog->struct_count + 1));
    for (size_t i = 0; i < prog->struct_count; i++) {
        L->structs[i].type = prog->struct_types[i];
        /* find original Decl to get raw attr list with gating preserved */
        for (size_t j = 0; j < prog->module->decls.len; j++) {
            Decl *d = prog->module->decls.data[j];
            if (d->kind != DK_STRUCT) continue;
            if (strcmp(d->struc.name, prog->struct_types[i]->struct_name) != 0) continue;
            const char *gates[16]; size_t gn = 0;
            for (size_t k = 0; k < d->struc.invariants.len; k++) {
                Attr *at = d->struc.invariants.data[k];
                if (!is_invariant_kind(at->name)) {
                    if (gn < 16) gates[gn++] = at->name;
                    continue;
                }
                if (!gates_active(&prog->active, gates, gn)) continue;
                if (strcmp(at->name, "invariant") != 0) continue;
                Invariant inv = { at->arg, at->loc, at->name };
                vec_push(&L->structs[i].invariants, inv);
            }
            break;
        }
    }
    return L;
}

LoweredFn *lower_find(LoweredProgram *L, FnVariant *v) {
    for (size_t i = 0; i < L->prog->fn_count; i++) {
        FnVariantSet vs = L->prog->fns[i];
        for (size_t j = 0; j < vs.count; j++) {
            if (&vs.items[j] == v) return L->fns[i][j];
        }
    }
    return NULL;
}
