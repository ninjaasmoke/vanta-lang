#include "sema.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Attribute sets
 * ============================================================ */

AttrSet attrset_make(Arena *a, const char **names, size_t n) {
    AttrSet s;
    s.names = (const char **)arena_alloc(a, sizeof(char *) * (n ? n : 1));
    s.count = n;
    for (size_t i = 0; i < n; i++) s.names[i] = arena_strdup(a, names[i]);
    return s;
}

int attrset_contains(const AttrSet *s, const char *name) {
    for (size_t i = 0; i < s->count; i++)
        if (strcmp(s->names[i], name) == 0) return 1;
    return 0;
}

int attrset_subset(const AttrSet *small, const AttrSet *big) {
    for (size_t i = 0; i < small->count; i++)
        if (!attrset_contains(big, small->names[i])) return 0;
    return 1;
}

/* ============================================================
 * Sema state
 * ============================================================ */

/* names known to be invariant attributes (do not gate variant selection) */
static int is_invariant_attr(const char *n) {
    return strcmp(n, "requires")  == 0
        || strcmp(n, "ensures")   == 0
        || strcmp(n, "invariant") == 0
        || strcmp(n, "constraint")== 0
        || strcmp(n, "attribute") == 0;
}

typedef struct Scope Scope;

typedef struct {
    const char *name;
    Type       *type;
    int         is_param;
} Sym;
typedef VEC(Sym) SymVec;

struct Scope {
    Scope *parent;
    SymVec syms;
};

typedef struct StructEntry {
    const char *name;
    Type       *type;
} StructEntry;
typedef VEC(StructEntry) StructEntryVec;

typedef struct AliasEntry {
    const char *name;
    AstType    *target;
} AliasEntry;
typedef VEC(AliasEntry) AliasEntryVec;

typedef struct VariantBucket {
    const char *name;
    FnVariant  *items;
    size_t      count;
    size_t      cap;
} VariantBucket;
typedef VEC(VariantBucket) VariantBucketVec;

struct Sema {
    Arena         *arena;
    Module        *module;
    const char    *path;
    AttrSet        active;

    StructEntryVec structs;
    AliasEntryVec  aliases;
    VariantBucketVec fns;

    /* primitive types interned once */
    Type *t_void, *t_bool;
    Type *t_int, *t_i32, *t_i64, *t_u32, *t_u64, *t_u8;
    Type *t_f32, *t_f64;
    Type *t_error;

    /* current function context */
    FnVariant *cur_fn;
    Scope     *scope;
    int        loop_depth;     /* >0 inside a while/for body */

    int had_error;
};

/* ============================================================
 * Diagnostics
 * ============================================================ */

static void sema_err(Sema *S, Loc loc, const char *fmt, ...) {
    /* if we already chose TY_ERROR for a sub-expr we sometimes still get
     * here once. cap at 50 errors to keep terminals readable. */
    static int max_reported = 50;
    S->had_error = 1;
    if (max_reported-- <= 0) return;
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "%s:%d:%d: type error: ",
            S->path ? S->path
                    : (S->module->name ? S->module->name : "<input>"),
            loc.line, loc.col);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

/* ============================================================
 * Type construction & resolution
 * ============================================================ */

static Type *new_prim(Arena *a, TypeKind k, size_t size) {
    Type *t = (Type *)arena_alloc_zero(a, sizeof(Type));
    t->kind = k;
    t->size = size;
    return t;
}

static void init_prims(Sema *S) {
    Arena *a = S->arena;
    S->t_void = new_prim(a, TY_VOID, 0);
    S->t_bool = new_prim(a, TY_BOOL, 1);
    S->t_int  = new_prim(a, TY_INT,  8);
    S->t_i32  = new_prim(a, TY_I32,  4);
    S->t_i64  = new_prim(a, TY_I64,  8);
    S->t_u32  = new_prim(a, TY_U32,  4);
    S->t_u64  = new_prim(a, TY_U64,  8);
    S->t_u8   = new_prim(a, TY_U8,   1);
    S->t_f32  = new_prim(a, TY_F32,  4);
    S->t_f64  = new_prim(a, TY_F64,  8);
    S->t_error= new_prim(a, TY_ERROR, 0);
}

static Type *prim_by_name(Sema *S, const char *n) {
    if (strcmp(n, "void") == 0) return S->t_void;
    if (strcmp(n, "bool") == 0) return S->t_bool;
    if (strcmp(n, "int")  == 0) return S->t_int;
    if (strcmp(n, "i32")  == 0) return S->t_i32;
    if (strcmp(n, "i64")  == 0) return S->t_i64;
    if (strcmp(n, "u32")  == 0) return S->t_u32;
    if (strcmp(n, "u64")  == 0) return S->t_u64;
    if (strcmp(n, "u8")   == 0) return S->t_u8;
    if (strcmp(n, "f32")  == 0) return S->t_f32;
    if (strcmp(n, "f64")  == 0) return S->t_f64;
    return NULL;
}

static Type *find_struct(Sema *S, const char *n) {
    for (size_t i = 0; i < S->structs.len; i++)
        if (strcmp(S->structs.data[i].name, n) == 0) return S->structs.data[i].type;
    return NULL;
}

static AstType *find_alias(Sema *S, const char *n) {
    for (size_t i = 0; i < S->aliases.len; i++)
        if (strcmp(S->aliases.data[i].name, n) == 0) return S->aliases.data[i].target;
    return NULL;
}

static Type *resolve_type(Sema *S, AstType *at);

static Type *resolve_named(Sema *S, AstType *at) {
    Type *p = prim_by_name(S, at->name);
    if (p) return p;
    Type *st = find_struct(S, at->name);
    if (st) return st;
    AstType *al = find_alias(S, at->name);
    if (al) return resolve_type(S, al);
    sema_err(S, at->loc, "unknown type '%s'", at->name);
    return S->t_error;
}

static Type *resolve_type(Sema *S, AstType *at) {
    if (!at) return S->t_void;
    switch (at->kind) {
    case AT_PRIM:
    case AT_NAMED: return resolve_named(S, at);
    case AT_PTR: {
        Type *t = (Type *)arena_alloc_zero(S->arena, sizeof(Type));
        t->kind = TY_PTR;
        t->elem = resolve_type(S, at->elem);
        t->size = 8;
        return t;
    }
    case AT_SLICE: {
        Type *t = (Type *)arena_alloc_zero(S->arena, sizeof(Type));
        t->kind = TY_SLICE;
        t->elem = resolve_type(S, at->elem);
        t->size = 16;   /* {ptr, len} */
        return t;
    }
    case AT_ARRAY: {
        Type *t = (Type *)arena_alloc_zero(S->arena, sizeof(Type));
        t->kind = TY_ARRAY;
        t->elem = resolve_type(S, at->elem);
        t->array_len = at->array_len;
        t->size = (size_t)at->array_len * (t->elem ? t->elem->size : 0);
        return t;
    }
    }
    return S->t_error;
}

/* ============================================================
 * Symbol table
 * ============================================================ */

static Scope *push_scope(Sema *S) {
    Scope *sc = (Scope *)arena_alloc_zero(S->arena, sizeof(Scope));
    sc->parent = S->scope;
    S->scope = sc;
    return sc;
}

static void pop_scope(Sema *S) {
    if (!S->scope) return;
    Scope *p = S->scope->parent;
    vec_free(&S->scope->syms);
    S->scope = p;
}

static void scope_add(Sema *S, const char *name, Type *t, int is_param) {
    Sym s = { name, t, is_param };
    vec_push(&S->scope->syms, s);
}

static Sym *scope_find(Sema *S, const char *name) {
    for (Scope *sc = S->scope; sc; sc = sc->parent) {
        for (size_t i = 0; i < sc->syms.len; i++) {
            if (strcmp(sc->syms.data[i].name, name) == 0)
                return &sc->syms.data[i];
        }
    }
    return NULL;
}

/* ============================================================
 * Variant book-keeping
 * ============================================================ */

static VariantBucket *fns_bucket(Sema *S, const char *name) {
    for (size_t i = 0; i < S->fns.len; i++) {
        if (strcmp(S->fns.data[i].name, name) == 0)
            return &S->fns.data[i];
    }
    VariantBucket nb = { name, NULL, 0, 0 };
    vec_push(&S->fns, nb);
    return &S->fns.data[S->fns.len - 1];
}

static void bucket_push(Arena *a, VariantBucket *b, FnVariant v) {
    if (b->count == b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 2;
        FnVariant *ni = (FnVariant *)arena_alloc(a, sizeof(FnVariant) * nc);
        if (b->items) memcpy(ni, b->items, sizeof(FnVariant) * b->count);
        b->items = ni;
        b->cap = nc;
    }
    b->items[b->count++] = v;
}

/* extract the gating attribute names from a Decl's attribute list.
 *
 * intuition: a non-invariant attr immediately followed by an invariant
 * attr (@requires/@ensures/@invariant) is gating *that contract*, not
 * the function variant. so '@debug @requires(x) fn foo()' makes foo
 * always selectable, with the contract gated by debug.
 *
 * a non-invariant attr followed by another non-invariant (or by EOL)
 * is a fn-variant gate, so '@debug fn foo()' still means
 * "this variant of foo only exists when debug is active".
 */
static AttrSet decl_required_attrs(Sema *S, AttrVec *attrs) {
    const char **names = (const char **)arena_alloc(S->arena, sizeof(char *) * (attrs->len + 1));
    size_t k = 0;
    for (size_t i = 0; i < attrs->len; i++) {
        Attr *a = attrs->data[i];
        if (is_invariant_attr(a->name)) continue;
        /* if the *next* attr is an invariant, this attr is the gate
         * for that invariant - not for the fn variant. skip it. */
        if (i + 1 < attrs->len && is_invariant_attr(attrs->data[i + 1]->name))
            continue;
        names[k++] = a->name;
    }
    AttrSet s;
    s.names = names;
    s.count = k;
    return s;
}

/* ============================================================
 * Pre-pass: collect structs, aliases, function signatures
 * ============================================================ */

static void collect_top_level(Sema *S) {
    /* aliases first (cheap forward-decl), then structs (so field types
     * may reference structs defined later), then signatures. */
    for (size_t i = 0; i < S->module->decls.len; i++) {
        Decl *d = S->module->decls.data[i];
        if (d->kind != DK_TYPE_ALIAS) continue;
        AliasEntry e = { d->alias.name, d->alias.aliased };
        vec_push(&S->aliases, e);
    }

    /* pre-register struct names so they can be referenced before their body
     * is resolved. fields filled in next pass. */
    for (size_t i = 0; i < S->module->decls.len; i++) {
        Decl *d = S->module->decls.data[i];
        if (d->kind != DK_STRUCT) continue;
        Type *t = (Type *)arena_alloc_zero(S->arena, sizeof(Type));
        t->kind = TY_STRUCT;
        t->struct_name = d->struc.name;
        StructEntry e = { d->struc.name, t };
        vec_push(&S->structs, e);
    }

    /* now fill struct fields */
    for (size_t i = 0; i < S->module->decls.len; i++) {
        Decl *d = S->module->decls.data[i];
        if (d->kind != DK_STRUCT) continue;
        Type *t = find_struct(S, d->struc.name);
        size_t off = 0;
        for (size_t j = 0; j < d->struc.fields.len; j++) {
            StructFieldDecl *fd = &d->struc.fields.data[j];
            Type *ft = resolve_type(S, fd->type);
            StructFieldInfo info = { fd->name, ft, off };
            vec_push(&t->fields, info);
            off += ft->size ? ft->size : 8;
        }
        t->size = off;
    }

    /* function variants */
    for (size_t i = 0; i < S->module->decls.len; i++) {
        Decl *d = S->module->decls.data[i];
        if (d->kind != DK_FN) continue;
        FnVariant v = {0};
        v.decl = d;
        v.required = decl_required_attrs(S, &d->attrs);
        v.param_count = d->fn.params.len;
        v.param_types = (Type **)arena_alloc(S->arena, sizeof(Type *) * (v.param_count + 1));
        for (size_t j = 0; j < v.param_count; j++)
            v.param_types[j] = resolve_type(S, d->fn.params.data[j].type);
        v.ret_type = d->fn.ret ? resolve_type(S, d->fn.ret) : S->t_void;
        VariantBucket *b = fns_bucket(S, d->fn.name);
        bucket_push(S->arena, b, v);
    }
}

/* ============================================================
 * Variant selection
 * ============================================================ */

/* Pick the variant whose required ⊆ active and whose required is largest.
 * Returns NULL if no match, sets *ambig = 1 on a tie. */
static FnVariant *select_variant(VariantBucket *b, const AttrSet *active, int *ambig) {
    FnVariant *best = NULL;
    int best_score = -1;
    int tied = 0;
    *ambig = 0;
    for (size_t i = 0; i < b->count; i++) {
        FnVariant *v = &b->items[i];
        if (!attrset_subset(&v->required, active)) continue;
        int score = (int)v->required.count;
        if (score > best_score) { best = v; best_score = score; tied = 0; }
        else if (score == best_score) tied = 1;
    }
    if (tied) *ambig = 1;
    return best;
}

/* ============================================================
 * Expression typing
 * ============================================================ */

static Type *check_expr(Sema *S, Expr *e);

static int is_lvalue(const Expr *e) {
    switch (e->kind) {
    case EX_IDENT: case EX_INDEX: case EX_FIELD: return 1;
    case EX_UNARY: return e->unary.op == OP_DEREF;
    default: return 0;
    }
}

static Type *check_assign(Sema *S, Expr *e) {
    Type *lt = check_expr(S, e->assign.lhs);
    Type *rt = check_expr(S, e->assign.rhs);
    if (!is_lvalue(e->assign.lhs))
        sema_err(S, e->loc, "left side of assignment is not addressable");
    if (!type_equals(lt, rt))
        sema_err(S, e->loc, "assignment type mismatch: %s vs %s",
                 type_name(lt), type_name(rt));
    e->resolved = lt;
    return lt;
}

static Type *check_binary(Sema *S, Expr *e) {
    Type *lt = check_expr(S, e->binary.l);
    Type *rt = check_expr(S, e->binary.r);
    Op op = e->binary.op;
    Type *t = S->t_error;
    switch (op) {
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
        if (!type_is_numeric(lt) || !type_is_numeric(rt))
            sema_err(S, e->loc, "arithmetic on non-numeric types (%s, %s)",
                     type_name(lt), type_name(rt));
        else if (!type_equals(lt, rt))
            sema_err(S, e->loc, "arithmetic type mismatch: %s vs %s",
                     type_name(lt), type_name(rt));
        t = lt;
        break;
    case OP_EQ: case OP_NEQ: case OP_LT: case OP_GT: case OP_LE: case OP_GE:
        if (!type_equals(lt, rt))
            sema_err(S, e->loc, "comparison type mismatch: %s vs %s",
                     type_name(lt), type_name(rt));
        t = S->t_bool;
        break;
    case OP_AND: case OP_OR:
        if (lt->kind != TY_BOOL || rt->kind != TY_BOOL)
            sema_err(S, e->loc, "logical op needs bools, got (%s, %s)",
                     type_name(lt), type_name(rt));
        t = S->t_bool;
        break;
    case OP_BITAND: case OP_BITOR: case OP_BITXOR:
    case OP_SHL: case OP_SHR:
        if (!type_is_integer(lt) || !type_is_integer(rt))
            sema_err(S, e->loc, "bitwise op needs integers, got (%s, %s)",
                     type_name(lt), type_name(rt));
        t = lt;
        break;
    default:
        sema_err(S, e->loc, "internal: unexpected binary op");
    }
    e->resolved = t;
    return t;
}

static Type *check_unary(Sema *S, Expr *e) {
    Type *xt = check_expr(S, e->unary.e);
    Type *t = S->t_error;
    switch (e->unary.op) {
    case OP_NEG:
        if (!type_is_numeric(xt))
            sema_err(S, e->loc, "negate needs numeric, got %s", type_name(xt));
        t = xt; break;
    case OP_NOT:
        /* '!x' on a pointer means 'x is null'. otherwise, must be a bool.
         * keeps 'if !p { ... }' working without a separate is_null call. */
        if (xt->kind == TY_PTR) { t = S->t_bool; break; }
        if (xt->kind != TY_BOOL)
            sema_err(S, e->loc, "'!' needs bool or pointer, got %s", type_name(xt));
        t = S->t_bool; break;
    case OP_BITNOT:
        if (!type_is_integer(xt))
            sema_err(S, e->loc, "'~' needs integer, got %s", type_name(xt));
        t = xt; break;
    case OP_DEREF:
        if (xt->kind != TY_PTR) {
            sema_err(S, e->loc, "cannot dereference non-pointer %s", type_name(xt));
            t = S->t_error;
        } else t = xt->elem;
        break;
    case OP_ADDR: {
        Type *p = (Type *)arena_alloc_zero(S->arena, sizeof(Type));
        p->kind = TY_PTR; p->elem = xt; p->size = 8;
        t = p; break;
    }
    default: break;
    }
    e->resolved = t;
    return t;
}

static Type *check_call(Sema *S, Expr *e) {
    /* The callee must be an identifier - Vanta has no first-class fns yet. */
    if (e->call.callee->kind != EX_IDENT) {
        check_expr(S, e->call.callee);
        sema_err(S, e->loc, "indirect calls not supported");
        e->resolved = S->t_error;
        return S->t_error;
    }
    const char *name = e->call.callee->ident;

    /* a couple of built-ins for the spec example: alloc, alloc_array, free */
    if (strcmp(name, "alloc_array") == 0) {
        if (e->call.args.len != 2) sema_err(S, e->loc, "alloc_array expects 2 args");
        else {
            /* arg 0 is a type-name; we accept it as an ident and don't try
             * to look it up as a value. fixme: a real type-arg syntax. */
            Type *elem = S->t_int;
            if (e->call.args.data[0]->kind == EX_IDENT) {
                Type *p = prim_by_name(S, e->call.args.data[0]->ident);
                if (p) elem = p;
                else {
                    Type *st = find_struct(S, e->call.args.data[0]->ident);
                    if (st) elem = st;
                }
                e->call.args.data[0]->resolved = elem;
            } else {
                check_expr(S, e->call.args.data[0]);
            }
            Type *n = check_expr(S, e->call.args.data[1]);
            if (!type_is_integer(n)) sema_err(S, e->loc, "alloc_array length must be int");

            Type *p = (Type *)arena_alloc_zero(S->arena, sizeof(Type));
            p->kind = TY_PTR; p->elem = elem; p->size = 8;
            e->resolved = p;
            return p;
        }
        Type *p = (Type *)arena_alloc_zero(S->arena, sizeof(Type));
        p->kind = TY_PTR; p->elem = S->t_int; p->size = 8;
        e->resolved = p;
        return p;
    }
    if (strcmp(name, "alloc") == 0) {
        /* alloc(T) -> *T. arg0 is a type-as-ident, same trick as alloc_array. */
        Type *elem = S->t_int;
        if (e->call.args.len >= 1 && e->call.args.data[0]->kind == EX_IDENT) {
            Type *p = prim_by_name(S, e->call.args.data[0]->ident);
            if (p) elem = p;
            else {
                Type *st = find_struct(S, e->call.args.data[0]->ident);
                if (st) elem = st;
            }
            e->call.args.data[0]->resolved = elem;
        }
        Type *p = (Type *)arena_alloc_zero(S->arena, sizeof(Type));
        p->kind = TY_PTR; p->elem = elem; p->size = 8;
        e->resolved = p;
        return p;
    }
    if (strcmp(name, "free") == 0) {
        for (size_t i = 0; i < e->call.args.len; i++)
            check_expr(S, e->call.args.data[i]);
        e->resolved = S->t_void;
        return S->t_void;
    }
    if (strcmp(name, "print") == 0 || strcmp(name, "println") == 0) {
        for (size_t i = 0; i < e->call.args.len; i++)
            check_expr(S, e->call.args.data[i]);
        e->resolved = S->t_void;
        return S->t_void;
    }

    VariantBucket *b = NULL;
    for (size_t i = 0; i < S->fns.len; i++)
        if (strcmp(S->fns.data[i].name, name) == 0) { b = &S->fns.data[i]; break; }
    if (!b) {
        sema_err(S, e->loc, "unknown function '%s'", name);
        e->resolved = S->t_error;
        return S->t_error;
    }
    int ambig = 0;
    FnVariant *v = select_variant(b, &S->active, &ambig);
    if (ambig)   sema_err(S, e->loc, "ambiguous variant for '%s'", name);
    if (!v)      { sema_err(S, e->loc, "no variant of '%s' matches active attributes", name);
                   e->resolved = S->t_error; return S->t_error; }

    /* attach the picked variant - interp uses this for direct dispatch */
    e->resolved_fn = v;

    if (e->call.args.len != v->param_count) {
        sema_err(S, e->loc, "'%s' expects %zu args, got %zu",
                 name, v->param_count, e->call.args.len);
    } else {
        for (size_t i = 0; i < v->param_count; i++) {
            Type *at = check_expr(S, e->call.args.data[i]);
            if (!type_equals(at, v->param_types[i]))
                sema_err(S, e->call.args.data[i]->loc,
                         "arg %zu of '%s': expected %s, got %s",
                         i, name, type_name(v->param_types[i]), type_name(at));
        }
    }
    e->resolved = v->ret_type;
    return v->ret_type;
}

static Type *check_index(Sema *S, Expr *e) {
    Type *bt = check_expr(S, e->index_.base);
    Type *it = check_expr(S, e->index_.index);
    if (!type_is_integer(it))
        sema_err(S, e->loc, "index must be integer, got %s", type_name(it));
    Type *t = S->t_error;
    if (bt->kind == TY_PTR || bt->kind == TY_SLICE || bt->kind == TY_ARRAY)
        t = bt->elem;
    else
        sema_err(S, e->loc, "cannot index %s", type_name(bt));
    e->resolved = t;
    return t;
}

static Type *check_field(Sema *S, Expr *e) {
    Type *bt = check_expr(S, e->field.base);
    /* allow ptr-to-struct auto-deref for `s.f` where s : *Stack */
    Type *target = bt;
    if (bt->kind == TY_PTR && bt->elem && bt->elem->kind == TY_STRUCT)
        target = bt->elem;
    if (target->kind != TY_STRUCT) {
        sema_err(S, e->loc, "cannot get field on %s", type_name(bt));
        e->resolved = S->t_error;
        return S->t_error;
    }
    for (size_t i = 0; i < target->fields.len; i++) {
        StructFieldInfo f = target->fields.data[i];
        if (strcmp(f.name, e->field.name) == 0) {
            e->resolved = f.type;
            return f.type;
        }
    }
    sema_err(S, e->loc, "no field '%s' on %s", e->field.name, type_name(target));
    e->resolved = S->t_error;
    return S->t_error;
}

static Type *check_struct_lit(Sema *S, Expr *e) {
    Type *t = find_struct(S, e->struct_lit.type_name);
    if (!t) {
        sema_err(S, e->loc, "unknown struct '%s'", e->struct_lit.type_name);
        e->resolved = S->t_error;
        return S->t_error;
    }
    for (size_t i = 0; i < e->struct_lit.fields.len; i++) {
        StructField sf = e->struct_lit.fields.data[i];
        Type *vt = check_expr(S, sf.value);
        Type *ft = NULL;
        for (size_t j = 0; j < t->fields.len; j++)
            if (strcmp(t->fields.data[j].name, sf.name) == 0) ft = t->fields.data[j].type;
        if (!ft) sema_err(S, e->loc, "no field '%s' on %s", sf.name, e->struct_lit.type_name);
        else if (!type_equals(vt, ft))
            sema_err(S, sf.value->loc, "field '%s': expected %s, got %s",
                     sf.name, type_name(ft), type_name(vt));
    }
    e->resolved = t;
    return t;
}

static Type *check_expr(Sema *S, Expr *e) {
    if (!e) return S->t_error;
    switch (e->kind) {
    case EX_INT:    e->resolved = S->t_int;  return S->t_int;
    case EX_FLOAT:  e->resolved = S->t_f64;  return S->t_f64;
    case EX_BOOL:   e->resolved = S->t_bool; return S->t_bool;
    case EX_STRING: {
        /* string literal: *u8 to a fresh byte buffer (interp builds it).
         * length is implicit; the user passes it explicitly when needed. */
        Type *p = (Type *)arena_alloc_zero(S->arena, sizeof(Type));
        p->kind = TY_PTR; p->elem = S->t_u8; p->size = 8;
        e->resolved = p; return p;
    }
    case EX_IDENT: {
        if (strcmp(e->ident, "null") == 0) {
            Type *p = (Type *)arena_alloc_zero(S->arena, sizeof(Type));
            p->kind = TY_PTR; p->elem = S->t_void; p->size = 8;
            e->resolved = p; return p;
        }
        Sym *s = scope_find(S, e->ident);
        if (!s) {
            sema_err(S, e->loc, "undefined name '%s'", e->ident);
            e->resolved = S->t_error;
            return S->t_error;
        }
        e->resolved = s->type;
        return s->type;
    }
    case EX_UNARY:    return check_unary(S, e);
    case EX_BINARY:   return check_binary(S, e);
    case EX_ASSIGN:   return check_assign(S, e);
    case EX_CALL:     return check_call(S, e);
    case EX_INDEX:    return check_index(S, e);
    case EX_FIELD:    return check_field(S, e);
    case EX_STRUCT_LIT: return check_struct_lit(S, e);
    case EX_RANGE: {
        Type *lo = check_expr(S, e->range.lo);
        Type *hi = check_expr(S, e->range.hi);
        if (!type_is_integer(lo) || !type_is_integer(hi))
            sema_err(S, e->loc, "range bounds must be integer");
        e->resolved = lo;
        return lo;
    }
    case EX_OLD: {
        /* type is the inner expr's; spec says only valid in @ensures, but
         * we don't gate that here yet. fixme. */
        Type *t = check_expr(S, e->old_.inner);
        e->resolved = t;
        return t;
    }
    }
    return S->t_error;
}

/* ============================================================
 * Statement typing
 * ============================================================ */

static void check_block(Sema *S, StmtVec *b);

static void check_stmt(Sema *S, Stmt *st) {
    switch (st->kind) {
    case ST_EXPR:
        if (st->expr) check_expr(S, st->expr);
        break;
    case ST_LET: {
        Type *declared = st->let_.type ? resolve_type(S, st->let_.type) : NULL;
        Type *init = st->let_.init ? check_expr(S, st->let_.init) : NULL;
        Type *t = declared ? declared : init;
        if (!t) {
            sema_err(S, st->loc, "let '%s' needs a type or initializer", st->let_.name);
            t = S->t_error;
        } else if (declared && init && !type_equals(declared, init)) {
            sema_err(S, st->loc, "let '%s': declared %s, init %s",
                     st->let_.name, type_name(declared), type_name(init));
        }
        scope_add(S, st->let_.name, t, 0);
        break;
    }
    case ST_BLOCK:
        push_scope(S);
        check_block(S, &st->block);
        pop_scope(S);
        break;
    case ST_IF: {
        Type *c = check_expr(S, st->if_.cond);
        if (c->kind != TY_BOOL && c->kind != TY_ERROR)
            sema_err(S, st->loc, "if cond must be bool");
        push_scope(S); check_block(S, &st->if_.then_b); pop_scope(S);
        if (st->if_.else_b.len) {
            push_scope(S); check_block(S, &st->if_.else_b); pop_scope(S);
        }
        break;
    }
    case ST_WHILE: {
        Type *c = check_expr(S, st->while_.cond);
        if (c->kind != TY_BOOL && c->kind != TY_ERROR)
            sema_err(S, st->loc, "while cond must be bool");
        push_scope(S);
        S->loop_depth++;
        check_block(S, &st->while_.body);
        S->loop_depth--;
        pop_scope(S);
        break;
    }
    case ST_FOR: {
        Type *r = check_expr(S, st->for_.range);
        push_scope(S);
        scope_add(S, st->for_.var, r, 0);
        S->loop_depth++;
        check_block(S, &st->for_.body);
        S->loop_depth--;
        pop_scope(S);
        break;
    }
    case ST_RETURN: {
        Type *want = S->cur_fn ? S->cur_fn->ret_type : S->t_void;
        if (st->expr) {
            Type *got = check_expr(S, st->expr);
            if (!type_equals(want, got))
                sema_err(S, st->loc, "return %s, want %s",
                         type_name(got), type_name(want));
        } else if (want->kind != TY_VOID) {
            sema_err(S, st->loc, "missing return value (want %s)", type_name(want));
        }
        break;
    }
    case ST_ASSERT: {
        Type *c = check_expr(S, st->expr);
        if (c->kind != TY_BOOL && c->kind != TY_ERROR)
            sema_err(S, st->loc, "assert cond must be bool, got %s", type_name(c));
        break;
    }
    case ST_MATCH: {
        Type *t = check_expr(S, st->match_.scrutinee);
        for (size_t i = 0; i < st->match_.arms.len; i++) {
            MatchArm a = st->match_.arms.data[i];
            if (!a.is_default) {
                Type *pt = check_expr(S, a.pattern);
                if (!type_equals(pt, t))
                    sema_err(S, a.pattern->loc, "match arm type mismatch");
            }
            push_scope(S); check_block(S, &a.body); pop_scope(S);
        }
        break;
    }
    case ST_BREAK:
    case ST_CONTINUE:
        if (S->loop_depth <= 0) {
            sema_err(S, st->loc,
                     "'%s' outside of a loop",
                     st->kind == ST_BREAK ? "break" : "continue");
        }
        break;
    }
}

static void check_block(Sema *S, StmtVec *b) {
    for (size_t i = 0; i < b->len; i++) check_stmt(S, b->data[i]);
}

/* ============================================================
 * Function bodies
 * ============================================================ */

static void check_fn_body(Sema *S, FnVariant *v) {
    S->cur_fn = v;
    push_scope(S);
    Decl *d = v->decl;
    for (size_t i = 0; i < d->fn.params.len; i++) {
        Param p = d->fn.params.data[i];
        scope_add(S, p.name, v->param_types[i], 1);
    }
    check_block(S, &d->fn.body);
    pop_scope(S);
    S->cur_fn = NULL;
}

/* ============================================================
 * Driver
 * ============================================================ */

SemaProgram *sema_analyze(Arena *arena, Module *m, const AttrSet *active) {
    return sema_analyze_with_path(arena, m, active, NULL);
}

SemaProgram *sema_analyze_with_path(Arena *arena, Module *m, const AttrSet *active,
                                     const char *path) {
    Sema S = {0};
    S.arena = arena;
    S.module = m;
    S.path = path;
    S.active = active ? *active : (AttrSet){0};
    init_prims(&S);

    collect_top_level(&S);

    /* type-check every variant body. we type-check ALL variants, not just
     * the selected ones - so the user gets errors in @release code even
     * during a @debug build. */
    for (size_t i = 0; i < S.fns.len; i++) {
        VariantBucket *b = &S.fns.data[i];
        for (size_t j = 0; j < b->count; j++) {
            check_fn_body(&S, &b->items[j]);
        }
    }

    SemaProgram *p = (SemaProgram *)arena_alloc_zero(arena, sizeof(SemaProgram));
    p->module = m;
    p->had_error = S.had_error;
    p->active = S.active;
    p->fn_count = S.fns.len;
    p->fns = (FnVariantSet *)arena_alloc(arena, sizeof(FnVariantSet) * (p->fn_count + 1));
    for (size_t i = 0; i < S.fns.len; i++) {
        p->fns[i].name = S.fns.data[i].name;
        p->fns[i].items = S.fns.data[i].items;
        p->fns[i].count = S.fns.data[i].count;
    }
    p->struct_count = S.structs.len;
    p->struct_types = (Type **)arena_alloc(arena, sizeof(Type *) * (p->struct_count + 1));
    for (size_t i = 0; i < S.structs.len; i++) p->struct_types[i] = S.structs.data[i].type;

    /* free transient vec storage */
    vec_free(&S.structs);
    vec_free(&S.aliases);
    vec_free(&S.fns);
    return p;
}

Type *sema_expr_type(Expr *e) { return e ? e->resolved : NULL; }
