#include "ast.h"

#include <stdio.h>
#include <string.h>

Expr *ast_expr(Arena *a, ExprKind k, Loc loc) {
    Expr *e = (Expr *)arena_alloc_zero(a, sizeof(Expr));
    e->kind = k;
    e->loc = loc;
    return e;
}

Stmt *ast_stmt(Arena *a, StmtKind k, Loc loc) {
    Stmt *s = (Stmt *)arena_alloc_zero(a, sizeof(Stmt));
    s->kind = k;
    s->loc = loc;
    return s;
}

Decl *ast_decl(Arena *a, DeclKind k, Loc loc) {
    Decl *d = (Decl *)arena_alloc_zero(a, sizeof(Decl));
    d->kind = k;
    d->loc = loc;
    return d;
}

Attr *ast_attr(Arena *a, Loc loc, const char *name, Expr *arg) {
    Attr *at = (Attr *)arena_alloc_zero(a, sizeof(Attr));
    at->loc = loc;
    at->name = name;
    at->arg = arg;
    return at;
}

AstType *ast_type(Arena *a, AstTypeKind k, Loc loc) {
    AstType *t = (AstType *)arena_alloc_zero(a, sizeof(AstType));
    t->kind = k;
    t->loc = loc;
    return t;
}

/* ----------------- printer ----------------- */

static void indent(int n) { for (int i = 0; i < n; i++) fputs("  ", stdout); }

static void print_type(const AstType *t) {
    if (!t) { fputs("<no-type>", stdout); return; }
    switch (t->kind) {
    case AT_PRIM:
    case AT_NAMED:
        fputs(t->name, stdout); break;
    case AT_PTR:
        fputc('*', stdout); print_type(t->elem); break;
    case AT_SLICE:
        fputs("[]", stdout); print_type(t->elem); break;
    case AT_ARRAY:
        printf("[%lld]", t->array_len); print_type(t->elem); break;
    }
}

static const char *op_str(Op o) {
    switch (o) {
    case OP_NEG: return "-"; case OP_NOT: return "!";
    case OP_DEREF: return "*"; case OP_ADDR: return "&";
    case OP_BITNOT: return "~";
    case OP_ADD: return "+"; case OP_SUB: return "-";
    case OP_MUL: return "*"; case OP_DIV: return "/"; case OP_MOD: return "%";
    case OP_BITAND: return "&"; case OP_BITOR: return "|"; case OP_BITXOR: return "^";
    case OP_SHL: return "<<"; case OP_SHR: return ">>";
    case OP_EQ: return "=="; case OP_NEQ: return "!=";
    case OP_LT: return "<"; case OP_GT: return ">";
    case OP_LE: return "<="; case OP_GE: return ">=";
    case OP_AND: return "&&"; case OP_OR: return "||";
    case OP_ASSIGN: return "=";
    case OP_ADDASSIGN: return "+="; case OP_SUBASSIGN: return "-=";
    case OP_MULASSIGN: return "*="; case OP_DIVASSIGN: return "/=";
    }
    return "?";
}

/* precedence level for output. only used by the printer, not the parser. */
static int op_prec(Op o) {
    switch (o) {
    case OP_OR:                                       return 1;
    case OP_AND:                                      return 2;
    case OP_BITOR:                                    return 3;
    case OP_BITXOR:                                   return 4;
    case OP_BITAND:                                   return 5;
    case OP_EQ: case OP_NEQ:                          return 6;
    case OP_LT: case OP_GT: case OP_LE: case OP_GE:   return 7;
    case OP_SHL: case OP_SHR:                         return 8;
    case OP_ADD: case OP_SUB:                         return 9;
    case OP_MUL: case OP_DIV: case OP_MOD:            return 10;
    default:                                          return 11; /* unary etc */
    }
}

/* prec of the outermost op of e, or +inf for atoms. */
static int expr_prec(const Expr *e) {
    if (!e) return 99;
    if (e->kind == EX_BINARY) return op_prec(e->binary.op);
    return 99;
}

static void print_expr(const Expr *e, int d);

static void print_expr_inline(const Expr *e) { print_expr(e, 0); }

/* like print_expr_inline, but wrap in parens if the child's prec is lower
 * than `parent_prec`. eliminates the parade of redundant ()s the old printer
 * produced for things like '(a + b) - 1'. */
static void print_sub(const Expr *child, int parent_prec) {
    if (expr_prec(child) < parent_prec) {
        fputc('(', stdout);
        print_expr_inline(child);
        fputc(')', stdout);
    } else {
        print_expr_inline(child);
    }
}

static void print_expr(const Expr *e, int d) {
    if (!e) { fputs("<null>", stdout); return; }
    switch (e->kind) {
    case EX_INT:    printf("%lld", e->int_val); break;
    case EX_FLOAT:  printf("%g", e->float_val); break;
    case EX_BOOL:   fputs(e->bool_val ? "true" : "false", stdout); break;
    case EX_STRING: printf("\"%s\"", e->str_val ? e->str_val : ""); break;
    case EX_IDENT:  fputs(e->ident, stdout); break;
    case EX_UNARY:
        fputs(op_str(e->unary.op), stdout);
        print_expr_inline(e->unary.e); break;
    case EX_BINARY: {
        int pp = op_prec(e->binary.op);
        /* left-assoc: left child at >= pp, right child at > pp. */
        print_sub(e->binary.l, pp);
        printf(" %s ", op_str(e->binary.op));
        print_sub(e->binary.r, pp + 1);
        break;
    }
    case EX_CALL:
        print_expr_inline(e->call.callee);
        fputc('(', stdout);
        for (size_t i = 0; i < e->call.args.len; i++) {
            if (i) fputs(", ", stdout);
            print_expr_inline(e->call.args.data[i]);
        }
        fputc(')', stdout); break;
    case EX_INDEX:
        print_expr_inline(e->index_.base);
        fputc('[', stdout); print_expr_inline(e->index_.index); fputc(']', stdout);
        break;
    case EX_FIELD:
        print_expr_inline(e->field.base);
        printf(".%s", e->field.name); break;
    case EX_ASSIGN:
        print_expr_inline(e->assign.lhs);
        printf(" %s ", op_str(e->assign.op));
        print_expr_inline(e->assign.rhs); break;
    case EX_RANGE:
        print_expr_inline(e->range.lo);
        fputs("..", stdout);
        print_expr_inline(e->range.hi); break;
    case EX_STRUCT_LIT:
        printf("%s { ", e->struct_lit.type_name);
        for (size_t i = 0; i < e->struct_lit.fields.len; i++) {
            if (i) fputs(", ", stdout);
            StructField sf = e->struct_lit.fields.data[i];
            printf("%s = ", sf.name);
            print_expr_inline(sf.value);
        }
        fputs(" }", stdout); break;
    case EX_OLD:
        fputs("old(", stdout); print_expr_inline(e->old_.inner); fputc(')', stdout);
        break;
    }
    (void)d;
}

static void print_block(const StmtVec *b, int d);

static void print_stmt(const Stmt *s, int d) {
    indent(d);
    switch (s->kind) {
    case ST_EXPR:
        print_expr_inline(s->expr); fputc('\n', stdout); break;
    case ST_LET:
        printf("let %s", s->let_.name);
        if (s->let_.type) { fputs(": ", stdout); print_type(s->let_.type); }
        if (s->let_.init) { fputs(" = ", stdout); print_expr_inline(s->let_.init); }
        fputc('\n', stdout); break;
    case ST_BLOCK:
        fputs("{\n", stdout);
        for (size_t i = 0; i < s->block.len; i++) print_stmt(s->block.data[i], d + 1);
        indent(d); fputs("}\n", stdout); break;
    case ST_IF:
        fputs("if ", stdout); print_expr_inline(s->if_.cond); fputc('\n', stdout);
        print_block(&s->if_.then_b, d);
        if (s->if_.else_b.len) {
            indent(d); fputs("else\n", stdout);
            print_block(&s->if_.else_b, d);
        }
        break;
    case ST_WHILE:
        fputs("while ", stdout); print_expr_inline(s->while_.cond); fputc('\n', stdout);
        print_block(&s->while_.body, d); break;
    case ST_FOR:
        printf("for %s in ", s->for_.var); print_expr_inline(s->for_.range); fputc('\n', stdout);
        print_block(&s->for_.body, d); break;
    case ST_RETURN:
        fputs("return", stdout);
        if (s->expr) { fputc(' ', stdout); print_expr_inline(s->expr); }
        fputc('\n', stdout); break;
    case ST_ASSERT:
        fputs("assert ", stdout); print_expr_inline(s->expr); fputc('\n', stdout); break;
    case ST_MATCH:
        fputs("match ", stdout); print_expr_inline(s->match_.scrutinee); fputs(" {\n", stdout);
        for (size_t i = 0; i < s->match_.arms.len; i++) {
            MatchArm arm = s->match_.arms.data[i];
            indent(d + 1);
            if (arm.is_default) fputs("_", stdout);
            else                 print_expr_inline(arm.pattern);
            fputs(" =>\n", stdout);
            print_block(&arm.body, d + 1);
        }
        indent(d); fputs("}\n", stdout); break;
    }
}

static void print_block(const StmtVec *b, int d) {
    indent(d); fputs("{\n", stdout);
    for (size_t i = 0; i < b->len; i++) print_stmt(b->data[i], d + 1);
    indent(d); fputs("}\n", stdout);
}

static void print_attrs(const AttrVec *a, int d) {
    for (size_t i = 0; i < a->len; i++) {
        Attr *at = a->data[i];
        indent(d); printf("@%s", at->name);
        if (at->arg) { fputc('(', stdout); print_expr_inline(at->arg); fputc(')', stdout); }
        fputc('\n', stdout);
    }
}

void ast_print_module(const Module *m) {
    printf("module %s\n", m->name ? m->name : "<unnamed>");
    for (size_t i = 0; i < m->decls.len; i++) {
        const Decl *d = m->decls.data[i];
        fputc('\n', stdout);
        print_attrs(&d->attrs, 0);
        switch (d->kind) {
        case DK_IMPORT:
            printf("import %s\n", d->import_.name); break;
        case DK_TYPE_ALIAS:
            printf("type %s = ", d->alias.name); print_type(d->alias.aliased); fputc('\n', stdout);
            break;
        case DK_STRUCT:
            printf("struct %s {\n", d->struc.name);
            for (size_t j = 0; j < d->struc.fields.len; j++) {
                StructFieldDecl f = d->struc.fields.data[j];
                indent(1); printf("%s: ", f.name); print_type(f.type); fputc('\n', stdout);
            }
            print_attrs(&d->struc.invariants, 1);
            fputs("}\n", stdout); break;
        case DK_FN:
            printf("fn %s(", d->fn.name);
            for (size_t j = 0; j < d->fn.params.len; j++) {
                if (j) fputs(", ", stdout);
                Param p = d->fn.params.data[j];
                printf("%s: ", p.name); print_type(p.type);
            }
            fputs(") -> ", stdout);
            if (d->fn.ret) print_type(d->fn.ret); else fputs("void", stdout);
            fputc('\n', stdout);
            print_block(&d->fn.body, 0);
            break;
        }
    }
}
