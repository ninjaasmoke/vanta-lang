#include "interp.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

/* ============================================================
 * Value model
 *
 * Vanta is statically typed; we still tag Values at runtime because
 * a tree-walking interpreter is much simpler when values are uniform.
 * ============================================================ */

typedef struct Value Value;

typedef enum {
    V_VOID,
    V_INT,
    V_FLOAT,
    V_BOOL,
    V_PTR,        /* points into the interp heap (Value*) */
    V_STRUCT,     /* fields stored inline */
    V_ARRAY,      /* contiguous Values + len, used by alloc_array */
    V_STRING
} ValueKind;

struct Value {
    ValueKind kind;
    union {
        long long  i;
        double     f;
        int        b;
        Value     *p;            /* V_PTR */
        struct {
            Value *fields;       /* nfield Values, name lookup via Type */
            const Type *type;
        } st;
        struct {
            Value *items;
            size_t len;
            const Type *elem;
        } arr;
        const char *s;
    };
};

static Value V_void(void)             { Value v = {0}; v.kind = V_VOID; return v; }
static Value V_int(long long x)       { Value v = {0}; v.kind = V_INT; v.i = x; return v; }
static Value V_float(double x)        { Value v = {0}; v.kind = V_FLOAT; v.f = x; return v; }
static Value V_bool(int x)            { Value v = {0}; v.kind = V_BOOL; v.b = !!x; return v; }
static Value V_ptr(Value *p)          { Value v = {0}; v.kind = V_PTR; v.p = p; return v; }
static Value V_string(const char *s)  { Value v = {0}; v.kind = V_STRING; v.s = s; return v; }

/* zero/default value for a given type */
static Value default_value(const Type *t) {
    Value v = {0};
    if (!t) return v;
    switch (t->kind) {
    case TY_BOOL:                              v.kind = V_BOOL; v.b = 0; break;
    case TY_INT: case TY_I32: case TY_I64:
    case TY_U32: case TY_U64:                  v.kind = V_INT; v.i = 0; break;
    case TY_F32: case TY_F64:                  v.kind = V_FLOAT; v.f = 0; break;
    case TY_PTR: case TY_SLICE: case TY_ARRAY: v.kind = V_PTR; v.p = NULL; break;
    case TY_STRUCT: {
        v.kind = V_STRUCT;
        v.st.type = t;
        v.st.fields = (Value *)calloc(t->fields.len ? t->fields.len : 1, sizeof(Value));
        for (size_t i = 0; i < t->fields.len; i++)
            v.st.fields[i] = default_value(t->fields.data[i].type);
        break;
    }
    default: v.kind = V_VOID; break;
    }
    return v;
}

/* ============================================================
 * Environment / scopes
 * ============================================================ */

typedef struct {
    const char *name;
    Value       value;
} Binding;

typedef struct Frame Frame;
struct Frame {
    Frame    *parent;
    Binding  *bindings;
    size_t    count;
    size_t    cap;
};

static Frame *frame_new(Frame *parent) {
    Frame *f = (Frame *)calloc(1, sizeof(Frame));
    f->parent = parent;
    return f;
}

static void frame_free(Frame *f) {
    /* note: V_STRUCT stores malloc'd fields. own them properly with a
     * deeper owner-tracking scheme later. for now we leak - it's fine for
     * the kinds of programs we run. fixme. */
    free(f->bindings);
    free(f);
}

static void bind(Frame *f, const char *name, Value v) {
    if (f->count == f->cap) {
        f->cap = f->cap ? f->cap * 2 : 8;
        f->bindings = (Binding *)realloc(f->bindings, sizeof(Binding) * f->cap);
    }
    f->bindings[f->count].name = name;
    f->bindings[f->count].value = v;
    f->count++;
}

static Binding *find(Frame *f, const char *name) {
    for (Frame *c = f; c; c = c->parent) {
        for (size_t i = 0; i < c->count; i++) {
            if (strcmp(c->bindings[i].name, name) == 0) return &c->bindings[i];
        }
    }
    return NULL;
}

/* ============================================================
 * Interp state
 * ============================================================ */

typedef struct Interp {
    LoweredProgram *L;
    jmp_buf         abort_jmp;
    int             exit_code;
} Interp;

static void die(Interp *I, Loc loc, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "runtime error at %d:%d: ", loc.line, loc.col);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    I->exit_code = 2;
    longjmp(I->abort_jmp, 1);
}

/* ============================================================
 * Expression evaluation
 *
 * We carry an `lvalue` flag along the recursion so assignments and
 * &x can grab a pointer instead of a copy.
 * ============================================================ */

typedef struct {
    int     is_lvalue;
    Value  *slot;    /* pointer into a binding/field/element */
} LRef;

static Value eval(Interp *I, Frame *f, Expr *e);
static LRef  eval_lref(Interp *I, Frame *f, Expr *e);
static Value call_fn(Interp *I, FnVariant *v, Value *args, size_t n, Loc call_loc);

static int truthy(Value v) {
    switch (v.kind) {
    case V_BOOL: return v.b;
    case V_INT:  return v.i != 0;
    default:     return 0;
    }
}

static Value bin_arith(Interp *I, Loc loc, Op op, Value a, Value b) {
    if (a.kind == V_FLOAT || b.kind == V_FLOAT) {
        double x = a.kind == V_FLOAT ? a.f : (double)a.i;
        double y = b.kind == V_FLOAT ? b.f : (double)b.i;
        switch (op) {
        case OP_ADD: return V_float(x + y);
        case OP_SUB: return V_float(x - y);
        case OP_MUL: return V_float(x * y);
        case OP_DIV: if (y == 0) die(I, loc, "division by zero"); return V_float(x / y);
        case OP_MOD: die(I, loc, "%% on float"); break;
        default: break;
        }
    }
    long long x = a.i, y = b.i;
    switch (op) {
    case OP_ADD: return V_int(x + y);
    case OP_SUB: return V_int(x - y);
    case OP_MUL: return V_int(x * y);
    case OP_DIV: if (y == 0) die(I, loc, "division by zero"); return V_int(x / y);
    case OP_MOD: if (y == 0) die(I, loc, "%% by zero"); return V_int(x % y);
    default: die(I, loc, "internal: not arith");
    }
    return V_void();
}

static Value bin_bitwise(Interp *I, Loc loc, Op op, Value a, Value b) {
    /* sema rejected non-integer; we just operate on the int payload. */
    unsigned long long x = (unsigned long long)a.i;
    unsigned long long y = (unsigned long long)b.i;
    switch (op) {
    case OP_BITAND: return V_int((long long)(x & y));
    case OP_BITOR:  return V_int((long long)(x | y));
    case OP_BITXOR: return V_int((long long)(x ^ y));
    case OP_SHL:    return V_int((long long)(x << (y & 63)));
    case OP_SHR:    return V_int((long long)(x >> (y & 63)));
    default: die(I, loc, "internal: not bitwise");
    }
    return V_void();
}

static int values_equal(Value a, Value b) {
    if (a.kind != b.kind) {
        /* allow int/float comparison */
        if ((a.kind == V_INT && b.kind == V_FLOAT) || (a.kind == V_FLOAT && b.kind == V_INT)) {
            double x = a.kind == V_FLOAT ? a.f : (double)a.i;
            double y = b.kind == V_FLOAT ? b.f : (double)b.i;
            return x == y;
        }
        return 0;
    }
    switch (a.kind) {
    case V_INT:   return a.i == b.i;
    case V_FLOAT: return a.f == b.f;
    case V_BOOL:  return a.b == b.b;
    case V_PTR:   return a.p == b.p;
    case V_VOID:  return 1;
    case V_STRING: return strcmp(a.s, b.s) == 0;
    default:      return 0;
    }
}

static int values_lt(Value a, Value b) {
    if (a.kind == V_FLOAT || b.kind == V_FLOAT) {
        double x = a.kind == V_FLOAT ? a.f : (double)a.i;
        double y = b.kind == V_FLOAT ? b.f : (double)b.i;
        return x < y;
    }
    return a.i < b.i;
}

static Value bin_cmp(Op op, Value a, Value b) {
    int eq = values_equal(a, b);
    int lt = values_lt(a, b);
    switch (op) {
    case OP_EQ:  return V_bool(eq);
    case OP_NEQ: return V_bool(!eq);
    case OP_LT:  return V_bool(lt);
    case OP_GT:  return V_bool(!lt && !eq);
    case OP_LE:  return V_bool(lt || eq);
    case OP_GE:  return V_bool(!lt);
    default:     return V_bool(0);
    }
}

static Value *struct_field_ptr(Value *target, const Type *t, const char *name) {
    if (target->kind == V_PTR) target = target->p;
    if (!target || target->kind != V_STRUCT) return NULL;
    for (size_t i = 0; i < t->fields.len; i++) {
        if (strcmp(t->fields.data[i].name, name) == 0)
            return &target->st.fields[i];
    }
    return NULL;
}

static LRef eval_lref(Interp *I, Frame *f, Expr *e) {
    LRef r = {0};
    switch (e->kind) {
    case EX_IDENT: {
        Binding *b = find(f, e->ident);
        if (!b) die(I, e->loc, "undefined '%s'", e->ident);
        r.is_lvalue = 1; r.slot = &b->value; return r;
    }
    case EX_INDEX: {
        Value base = eval(I, f, e->index_.base);
        Value ix   = eval(I, f, e->index_.index);
        long long i = ix.i;
        if (base.kind == V_PTR) {
            if (!base.p) die(I, e->loc, "nil deref");
            if (base.p->kind == V_ARRAY) {
                if (i < 0 || (size_t)i >= base.p->arr.len)
                    die(I, e->loc, "out of bounds: %lld / %zu", i, base.p->arr.len);
                r.is_lvalue = 1; r.slot = &base.p->arr.items[i]; return r;
            }
            /* pointer into a Value array - treat as offset */
            r.is_lvalue = 1; r.slot = &base.p[i]; return r;
        }
        die(I, e->loc, "indexing non-pointer/array");
        return r;
    }
    case EX_FIELD: {
        /* need lref to base, then offset to field */
        Expr *base_e = e->field.base;
        LRef base;
        if (base_e->kind == EX_IDENT) base = eval_lref(I, f, base_e);
        else { Value v = eval(I, f, base_e); base.is_lvalue = 0; (void)v; }
        Value *target;
        const Type *t = NULL;
        if (base.is_lvalue) {
            target = base.slot;
            if (target->kind == V_PTR) { target = target->p; }
            if (target && target->kind == V_STRUCT) t = target->st.type;
        } else {
            die(I, e->loc, "cannot take field on rvalue");
            return r;
        }
        if (!t) die(I, e->loc, "field on non-struct");
        Value *fp = struct_field_ptr(target, t, e->field.name);
        if (!fp) die(I, e->loc, "no field '%s'", e->field.name);
        r.is_lvalue = 1; r.slot = fp; return r;
    }
    case EX_UNARY:
        if (e->unary.op == OP_DEREF) {
            Value p = eval(I, f, e->unary.e);
            if (p.kind != V_PTR || !p.p) die(I, e->loc, "deref nil");
            r.is_lvalue = 1; r.slot = p.p; return r;
        }
        break;
    default: break;
    }
    die(I, e->loc, "expression is not addressable");
    return r;
}

static Value eval_assign(Interp *I, Frame *f, Expr *e) {
    LRef lhs = eval_lref(I, f, e->assign.lhs);
    Value rhs = eval(I, f, e->assign.rhs);
    if (!lhs.is_lvalue || !lhs.slot) die(I, e->loc, "lhs not addressable");
    if (e->assign.op != OP_ASSIGN) {
        Op binop;
        switch (e->assign.op) {
        case OP_ADDASSIGN: binop = OP_ADD; break;
        case OP_SUBASSIGN: binop = OP_SUB; break;
        case OP_MULASSIGN: binop = OP_MUL; break;
        case OP_DIVASSIGN: binop = OP_DIV; break;
        default: die(I, e->loc, "bad compound assign"); return V_void();
        }
        rhs = bin_arith(I, e->loc, binop, *lhs.slot, rhs);
    }
    *lhs.slot = rhs;

    /* after a field write on a struct, re-check that struct's invariants.
     * spec: invariants run after every mutation point of the struct. */
    if (e->assign.lhs->kind == EX_FIELD) {
        Value base = eval(I, f, e->assign.lhs->field.base);
        Value *target = &base;
        if (target->kind == V_PTR) target = target->p;
        if (target && target->kind == V_STRUCT && target->st.type) {
            LoweredStruct *ls = lower_find_struct(I->L, target->st.type);
            if (ls) {
                Frame *inv = frame_new(NULL);
                /* bind each field by name so 'lo <= hi' resolves. */
                for (size_t i = 0; i < target->st.type->fields.len; i++) {
                    bind(inv,
                         target->st.type->fields.data[i].name,
                         target->st.fields[i]);
                }
                for (size_t i = 0; i < ls->invariants.len; i++) {
                    Value c = eval(I, inv, ls->invariants.data[i].cond);
                    if (!truthy(c)) {
                        frame_free(inv);
                        die(I, ls->invariants.data[i].loc,
                            "@invariant failed on %s",
                            target->st.type->struct_name
                                ? target->st.type->struct_name : "<struct>");
                    }
                }
                frame_free(inv);
            }
        }
    }

    return rhs;
}

/* find an FnVariant by name when no resolved_fn is attached (e.g.,
 * an invariant references a fn - rare). */
static FnVariant *lookup_active_variant(Interp *I, const char *name) {
    for (size_t i = 0; i < I->L->prog->fn_count; i++) {
        FnVariantSet vs = I->L->prog->fns[i];
        if (strcmp(vs.name, name) != 0) continue;
        int ambig = 0;
        FnVariant *best = NULL; int score = -1;
        for (size_t j = 0; j < vs.count; j++) {
            FnVariant *v = &vs.items[j];
            if (!attrset_subset(&v->required, &I->L->prog->active)) continue;
            int s = (int)v->required.count;
            if (s > score) { best = v; score = s; ambig = 0; }
            else if (s == score) ambig = 1;
        }
        (void)ambig;
        return best;
    }
    return NULL;
}

static Value eval_call(Interp *I, Frame *f, Expr *e) {
    /* builtins */
    if (e->call.callee->kind == EX_IDENT) {
        const char *n = e->call.callee->ident;
        if (strcmp(n, "alloc_array") == 0) {
            /* arg0 = type ident, arg1 = length expr */
            const Type *elem = NULL;
            if (e->call.args.data[0]->resolved) elem = e->call.args.data[0]->resolved;
            Value len = eval(I, f, e->call.args.data[1]);
            Value *arr = (Value *)calloc(1, sizeof(Value));
            arr->kind = V_ARRAY;
            arr->arr.len = (size_t)len.i;
            arr->arr.elem = elem;
            arr->arr.items = (Value *)calloc(len.i ? (size_t)len.i : 1, sizeof(Value));
            for (long long i = 0; i < len.i; i++)
                arr->arr.items[i] = elem ? default_value(elem) : V_int(0);
            return V_ptr(arr);
        }
        if (strcmp(n, "alloc") == 0) {
            /* alloc(T) -> *T. one fresh default-valued cell. */
            const Type *elem = NULL;
            if (e->call.args.len >= 1 && e->call.args.data[0]->resolved)
                elem = e->call.args.data[0]->resolved;
            Value *cell = (Value *)calloc(1, sizeof(Value));
            *cell = elem ? default_value(elem) : V_int(0);
            return V_ptr(cell);
        }
        if (strcmp(n, "free") == 0) {
            for (size_t i = 0; i < e->call.args.len; i++) eval(I, f, e->call.args.data[i]);
            /* leak - see fixme */
            return V_void();
        }
        if (strcmp(n, "print") == 0 || strcmp(n, "println") == 0) {
            for (size_t i = 0; i < e->call.args.len; i++) {
                if (i) fputc(' ', stdout);
                Value v = eval(I, f, e->call.args.data[i]);
                switch (v.kind) {
                case V_INT:    printf("%lld", v.i); break;
                case V_FLOAT:  printf("%g", v.f); break;
                case V_BOOL:   fputs(v.b ? "true" : "false", stdout); break;
                case V_STRING: fputs(v.s, stdout); break;
                case V_PTR:
                    /* *u8 buffer? print as bytes. otherwise dump the pointer. */
                    if (v.p && v.p->kind == V_ARRAY) {
                        for (size_t j = 0; j < v.p->arr.len; j++) {
                            int byte = (int)(v.p->arr.items[j].i & 0xFF);
                            fputc(byte, stdout);
                        }
                    } else {
                        printf("<ptr %p>", (void *)v.p);
                    }
                    break;
                default:       fputs("<?>", stdout);
                }
            }
            if (strcmp(n, "println") == 0) fputc('\n', stdout);
            return V_void();
        }
    }
    FnVariant *v = (FnVariant *)e->resolved_fn;
    if (!v) {
        if (e->call.callee->kind == EX_IDENT)
            v = lookup_active_variant(I, e->call.callee->ident);
    }
    if (!v) die(I, e->loc, "no callable");

    Value args[16];
    size_t n = e->call.args.len;
    if (n > 16) die(I, e->loc, "too many args");
    for (size_t i = 0; i < n; i++) args[i] = eval(I, f, e->call.args.data[i]);
    return call_fn(I, v, args, n, e->loc);
}

static Value eval(Interp *I, Frame *f, Expr *e) {
    if (!e) return V_void();
    switch (e->kind) {
    case EX_INT:    return V_int(e->int_val);
    case EX_FLOAT:  return V_float(e->float_val);
    case EX_BOOL:   return V_bool(e->bool_val);
    case EX_STRING: {
        /* lower a string literal to a fresh *u8 buffer.
         * each evaluation gets its own array so reverse(s,n) can mutate
         * without poisoning the source. cheap and dumb; no interning. */
        const char *s = e->str_val ? e->str_val : "";
        size_t n = strlen(s);
        Value *arr = (Value *)calloc(1, sizeof(Value));
        arr->kind = V_ARRAY;
        arr->arr.len = n;
        arr->arr.elem = NULL;
        arr->arr.items = (Value *)calloc(n ? n : 1, sizeof(Value));
        for (size_t i = 0; i < n; i++)
            arr->arr.items[i] = V_int((unsigned char)s[i]);
        return V_ptr(arr);
    }
    case EX_IDENT: {
        if (strcmp(e->ident, "null") == 0) return V_ptr(NULL);
        Binding *b = find(f, e->ident);
        if (!b) die(I, e->loc, "undefined '%s'", e->ident);
        return b->value;
    }
    case EX_UNARY: {
        if (e->unary.op == OP_ADDR) {
            LRef r = eval_lref(I, f, e->unary.e);
            return V_ptr(r.slot);
        }
        Value x = eval(I, f, e->unary.e);
        switch (e->unary.op) {
        case OP_NEG:
            if (x.kind == V_FLOAT) return V_float(-x.f);
            return V_int(-x.i);
        case OP_NOT:
            /* '!p' on a pointer is the nullness check. on a bool, the usual. */
            if (x.kind == V_PTR) return V_bool(x.p == NULL);
            return V_bool(!truthy(x));
        case OP_BITNOT:
            return V_int(~x.i);
        case OP_DEREF:
            if (x.kind != V_PTR || !x.p) die(I, e->loc, "deref nil");
            return *x.p;
        default: break;
        }
        return V_void();
    }
    case EX_BINARY: {
        Op op = e->binary.op;
        if (op == OP_AND) {
            Value a = eval(I, f, e->binary.l);
            if (!truthy(a)) return V_bool(0);
            return V_bool(truthy(eval(I, f, e->binary.r)));
        }
        if (op == OP_OR) {
            Value a = eval(I, f, e->binary.l);
            if (truthy(a)) return V_bool(1);
            return V_bool(truthy(eval(I, f, e->binary.r)));
        }
        Value a = eval(I, f, e->binary.l);
        Value b = eval(I, f, e->binary.r);
        switch (op) {
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
            return bin_arith(I, e->loc, op, a, b);
        case OP_BITAND: case OP_BITOR: case OP_BITXOR:
        case OP_SHL: case OP_SHR:
            return bin_bitwise(I, e->loc, op, a, b);
        case OP_EQ: case OP_NEQ: case OP_LT: case OP_GT: case OP_LE: case OP_GE:
            return bin_cmp(op, a, b);
        default: break;
        }
        return V_void();
    }
    case EX_ASSIGN: return eval_assign(I, f, e);
    case EX_CALL:   return eval_call(I, f, e);
    case EX_INDEX: {
        LRef r = eval_lref(I, f, e);
        return *r.slot;
    }
    case EX_FIELD: {
        LRef r = eval_lref(I, f, e);
        return *r.slot;
    }
    case EX_RANGE: {
        /* return a small synthetic struct; for-loops handle this directly */
        Value v = {0}; v.kind = V_STRUCT; v.st.type = NULL;
        v.st.fields = (Value *)calloc(2, sizeof(Value));
        v.st.fields[0] = eval(I, f, e->range.lo);
        v.st.fields[1] = eval(I, f, e->range.hi);
        return v;
    }
    case EX_STRUCT_LIT: {
        Value v = {0}; v.kind = V_STRUCT;
        v.st.type = e->resolved;
        const Type *t = e->resolved;
        v.st.fields = (Value *)calloc(t && t->fields.len ? t->fields.len : 1, sizeof(Value));
        if (t) for (size_t i = 0; i < t->fields.len; i++)
            v.st.fields[i] = default_value(t->fields.data[i].type);
        for (size_t i = 0; i < e->struct_lit.fields.len; i++) {
            StructField sf = e->struct_lit.fields.data[i];
            Value val = eval(I, f, sf.value);
            if (t) for (size_t j = 0; j < t->fields.len; j++) {
                if (strcmp(t->fields.data[j].name, sf.name) == 0) {
                    v.st.fields[j] = val; break;
                }
            }
        }
        return v;
    }
    case EX_OLD: {
        /* old() is captured into a binding called "_old_<addr>" before the
         * body runs; we look it up by pointer-as-key. */
        char buf[32];
        snprintf(buf, sizeof(buf), "__old_%p", (void *)e);
        Binding *b = find(f, buf);
        if (b) return b->value;
        /* fallback: just evaluate (won't be useful, but safe). */
        return eval(I, f, e->old_.inner);
    }
    }
    return V_void();
}

/* ============================================================
 * Statement execution
 * ============================================================ */

typedef enum { GO, RETURNED, BROKE, CONTINUED } Flow;

static Flow exec_block(Interp *I, Frame *f, StmtVec *body, Value *retval);

static Flow exec_stmt(Interp *I, Frame *f, Stmt *s, Value *retval) {
    switch (s->kind) {
    case ST_EXPR:
        if (s->expr) eval(I, f, s->expr);
        break;
    case ST_LET: {
        Value v = s->let_.init ? eval(I, f, s->let_.init)
                               : default_value(s->let_.type ? NULL : NULL);
        bind(f, s->let_.name, v);
        break;
    }
    case ST_BLOCK: {
        Frame *inner = frame_new(f);
        Flow fl = exec_block(I, inner, &s->block, retval);
        frame_free(inner);
        if (fl != GO) return fl;
        break;
    }
    case ST_IF: {
        Value c = eval(I, f, s->if_.cond);
        StmtVec *body = truthy(c) ? &s->if_.then_b : &s->if_.else_b;
        Frame *inner = frame_new(f);
        Flow fl = exec_block(I, inner, body, retval);
        frame_free(inner);
        if (fl != GO) return fl;
        break;
    }
    case ST_WHILE: {
        for (;;) {
            Value c = eval(I, f, s->while_.cond);
            if (!truthy(c)) break;
            Frame *inner = frame_new(f);
            Flow fl = exec_block(I, inner, &s->while_.body, retval);
            frame_free(inner);
            if (fl == RETURNED) return RETURNED;
            if (fl == BROKE)    break;
            /* CONTINUED falls through to next iteration */
        }
        break;
    }
    case ST_FOR: {
        /* range */
        Value r = eval(I, f, s->for_.range);
        if (r.kind != V_STRUCT || !r.st.fields)
            die(I, s->loc, "for-range expected");
        long long lo = r.st.fields[0].i;
        long long hi = r.st.fields[1].i;
        for (long long i = lo; i < hi; i++) {
            Frame *inner = frame_new(f);
            bind(inner, s->for_.var, V_int(i));
            Flow fl = exec_block(I, inner, &s->for_.body, retval);
            frame_free(inner);
            if (fl == RETURNED) return RETURNED;
            if (fl == BROKE)    break;
        }
        break;
    }
    case ST_RETURN:
        *retval = s->expr ? eval(I, f, s->expr) : V_void();
        return RETURNED;
    case ST_BREAK:    return BROKE;
    case ST_CONTINUE: return CONTINUED;
    case ST_ASSERT: {
        Value c = eval(I, f, s->expr);
        if (!truthy(c)) die(I, s->loc, "assertion failed");
        break;
    }
    case ST_MATCH: {
        Value sc = eval(I, f, s->match_.scrutinee);
        for (size_t i = 0; i < s->match_.arms.len; i++) {
            MatchArm a = s->match_.arms.data[i];
            if (!a.is_default) {
                Value pv = eval(I, f, a.pattern);
                if (!values_equal(sc, pv)) continue;
            }
            Frame *inner = frame_new(f);
            Flow fl = exec_block(I, inner, &a.body, retval);
            frame_free(inner);
            if (fl != GO) return fl;
            return GO;
        }
        break;
    }
    }
    return GO;
}

static Flow exec_block(Interp *I, Frame *f, StmtVec *body, Value *retval) {
    for (size_t i = 0; i < body->len; i++) {
        Flow fl = exec_stmt(I, f, body->data[i], retval);
        if (fl != GO) return fl;
    }
    return GO;
}

/* ============================================================
 * Function call (with invariants)
 * ============================================================ */

static Value call_fn(Interp *I, FnVariant *v, Value *args, size_t n, Loc call_loc) {
    Frame *f = frame_new(NULL);
    Decl *d = v->decl;
    for (size_t i = 0; i < d->fn.params.len && i < n; i++)
        bind(f, d->fn.params.data[i].name, args[i]);

    LoweredFn *lf = lower_find(I->L, v);

    /* @requires */
    if (lf) for (size_t i = 0; i < lf->requires_.len; i++) {
        Value c = eval(I, f, lf->requires_.data[i].cond);
        if (!truthy(c)) {
            frame_free(f);
            die(I, lf->requires_.data[i].loc, "@requires failed in '%s'", d->fn.name);
        }
    }

    /* capture old() values */
    if (lf) for (size_t i = 0; i < lf->old_exprs.len; i++) {
        Expr *o = lf->old_exprs.data[i];   /* this is the EX_OLD node */
        char buf[32];
        snprintf(buf, sizeof(buf), "__old_%p", (void *)o);
        Value cap = eval(I, f, o->old_.inner);
        char *key = (char *)malloc(strlen(buf) + 1);
        memcpy(key, buf, strlen(buf) + 1);
        bind(f, key, cap);
    }

    Value ret = V_void();
    exec_block(I, f, &d->fn.body, &ret);

    /* @ensures - make `result` visible inside the postcondition. */
    if (lf && lf->ensures.len > 0) {
        bind(f, "result", ret);
    }
    if (lf) for (size_t i = 0; i < lf->ensures.len; i++) {
        Value c = eval(I, f, lf->ensures.data[i].cond);
        if (!truthy(c)) {
            frame_free(f);
            die(I, lf->ensures.data[i].loc, "@ensures failed in '%s'", d->fn.name);
        }
    }
    frame_free(f);
    (void)call_loc;
    return ret;
}

/* ============================================================
 * Driver
 * ============================================================ */

int interp_run(LoweredProgram *L) {
    Interp I = {0};
    I.L = L;

    /* find main */
    FnVariant *main_v = lookup_active_variant(&I, "main");
    if (!main_v) {
        fputs("no main()\n", stderr);
        return 1;
    }

    if (setjmp(I.abort_jmp)) {
        return I.exit_code ? I.exit_code : 2;
    }

    Loc dummy = {0};
    Value ret = call_fn(&I, main_v, NULL, 0, dummy);
    if (ret.kind == V_INT)  return (int)ret.i;
    if (ret.kind == V_BOOL) return ret.b ? 0 : 1;
    return 0;
}
