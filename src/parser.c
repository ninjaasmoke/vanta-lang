#include "parser.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Recursive-descent parser. Pratt-style for expressions.
 * Identifiers and string slices that survive in the AST are arena-duped
 * out of the source buffer.
 */

typedef struct {
    const TokenVec *toks;
    size_t          pos;
    Arena          *arena;
    const char     *filename;
    int             had_error;
    int             no_struct_lit; /* >0: don't treat `Name {` as a struct lit.
                                      raised inside `if/while/for` conditions
                                      so the trailing `{` of the body wins. */
} P;

/* ----- diagnostics & token nav ----- */

static const Token *peek(P *p)         { return &p->toks->data[p->pos]; }
static const Token *peek_n(P *p, int n){ return &p->toks->data[p->pos + n]; }

static const Token *advance(P *p) {
    const Token *t = &p->toks->data[p->pos];
    if (t->kind != TK_EOF) p->pos++;
    return t;
}

static int check(P *p, TokenKind k)  { return peek(p)->kind == k; }
static int match(P *p, TokenKind k)  { if (check(p, k)) { advance(p); return 1; } return 0; }

static void error_at(P *p, const Token *t, const char *fmt, ...) {
    p->had_error = 1;
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "%s:%d:%d: parse error: ", p->filename, t->line, t->col);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static const Token *expect(P *p, TokenKind k, const char *what) {
    if (check(p, k)) return advance(p);
    error_at(p, peek(p), "expected %s, got '%s'", what, token_kind_name(peek(p)->kind));
    return peek(p);
}

static Loc loc_of(const Token *t) { Loc l; l.line = t->line; l.col = t->col; return l; }

static const char *dup_ident(P *p, const Token *t) {
    return arena_strndup(p->arena, t->start, t->len);
}

/* ----- string literal escape decoding ----- */
static const char *dup_string_lit(P *p, const Token *t) {
    /* token spans the quotes; strip them and decode escapes */
    if (t->len < 2) return arena_strdup(p->arena, "");
    const char *s = t->start + 1;
    size_t n = t->len - 2;
    char *buf = (char *)arena_alloc(p->arena, n + 1);
    size_t out = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < n) {
            char nx = s[++i];
            switch (nx) {
            case 'n':  buf[out++] = '\n'; break;
            case 't':  buf[out++] = '\t'; break;
            case 'r':  buf[out++] = '\r'; break;
            case '\\': buf[out++] = '\\'; break;
            case '"':  buf[out++] = '"';  break;
            case '0':  buf[out++] = '\0'; break;
            default:   buf[out++] = nx;   break;
            }
        } else {
            buf[out++] = c;
        }
    }
    buf[out] = '\0';
    return buf;
}

/* forward decls */
static AstType *parse_type(P *p);
static Expr    *parse_expr(P *p);
static Stmt    *parse_stmt(P *p);
static void     parse_block(P *p, StmtVec *out);
static Decl    *parse_decl(P *p, AttrVec attrs);
static Attr    *parse_attr(P *p);

/* ----- types ----- */

static int is_prim_name(const char *s, size_t n) {
    static const char *prims[] = {
        "int", "i32", "i64", "u32", "u64", "f32", "f64", "bool", "void", NULL
    };
    for (int i = 0; prims[i]; i++)
        if (strlen(prims[i]) == n && memcmp(prims[i], s, n) == 0) return 1;
    return 0;
}

static AstType *parse_type(P *p) {
    const Token *t = peek(p);
    Loc loc = loc_of(t);
    if (match(p, TK_STAR)) {
        AstType *ty = ast_type(p->arena, AT_PTR, loc);
        ty->elem = parse_type(p);
        return ty;
    }
    if (match(p, TK_LBRACK)) {
        if (check(p, TK_RBRACK)) {
            advance(p);
            AstType *ty = ast_type(p->arena, AT_SLICE, loc);
            ty->elem = parse_type(p);
            return ty;
        }
        const Token *n = expect(p, TK_INT, "array length");
        long long len = n->int_val;
        expect(p, TK_RBRACK, "']'");
        AstType *ty = ast_type(p->arena, AT_ARRAY, loc);
        ty->array_len = len;
        ty->elem = parse_type(p);
        return ty;
    }
    if (check(p, TK_IDENT)) {
        const Token *id = advance(p);
        AstTypeKind k = is_prim_name(id->start, id->len) ? AT_PRIM : AT_NAMED;
        AstType *ty = ast_type(p->arena, k, loc);
        ty->name = dup_ident(p, id);
        return ty;
    }
    error_at(p, t, "expected a type");
    advance(p);
    return ast_type(p->arena, AT_PRIM, loc);
}

/* ----- expressions (Pratt) ----- */

typedef enum {
    PREC_NONE,
    PREC_ASSIGN,    /* = += etc */
    PREC_OR,        /* || */
    PREC_AND,       /* && */
    PREC_BIT_OR,    /* |  */
    PREC_BIT_XOR,   /* ^  */
    PREC_BIT_AND,   /* &  (binary) */
    PREC_EQ,        /* == != */
    PREC_CMP,       /* < > <= >= */
    PREC_RANGE,     /* .. */
    PREC_SHIFT,     /* << >> */
    PREC_TERM,      /* + - */
    PREC_FACTOR,    /* * / % */
    PREC_UNARY,     /* ! - * & ~ */
    PREC_CALL,      /* () [] . */
    PREC_PRIMARY
} Prec;

static int op_precedence(TokenKind k) {
    switch (k) {
    case TK_ASSIGN: case TK_PLUSEQ: case TK_MINUSEQ:
    case TK_STAREQ: case TK_SLASHEQ:                      return PREC_ASSIGN;
    case TK_OR:                                           return PREC_OR;
    case TK_AND:                                          return PREC_AND;
    case TK_PIPE:                                         return PREC_BIT_OR;
    case TK_CARET:                                        return PREC_BIT_XOR;
    case TK_AMP:                                          return PREC_BIT_AND;
    case TK_EQ: case TK_NEQ:                              return PREC_EQ;
    case TK_LT: case TK_GT: case TK_LE: case TK_GE:       return PREC_CMP;
    case TK_DOTDOT:                                       return PREC_RANGE;
    case TK_SHL: case TK_SHR:                             return PREC_SHIFT;
    case TK_PLUS: case TK_MINUS:                          return PREC_TERM;
    case TK_STAR: case TK_SLASH: case TK_PERCENT:         return PREC_FACTOR;
    case TK_LPAREN: case TK_LBRACK: case TK_DOT:          return PREC_CALL;
    default: return PREC_NONE;
    }
}

static Op tok_to_binop(TokenKind k) {
    switch (k) {
    case TK_PLUS: return OP_ADD; case TK_MINUS: return OP_SUB;
    case TK_STAR: return OP_MUL; case TK_SLASH: return OP_DIV;
    case TK_PERCENT: return OP_MOD;
    case TK_AMP: return OP_BITAND; case TK_PIPE: return OP_BITOR;
    case TK_CARET: return OP_BITXOR;
    case TK_SHL: return OP_SHL;   case TK_SHR: return OP_SHR;
    case TK_EQ: return OP_EQ;   case TK_NEQ: return OP_NEQ;
    case TK_LT: return OP_LT;   case TK_GT: return OP_GT;
    case TK_LE: return OP_LE;   case TK_GE: return OP_GE;
    case TK_AND: return OP_AND; case TK_OR: return OP_OR;
    case TK_ASSIGN:  return OP_ASSIGN;
    case TK_PLUSEQ:  return OP_ADDASSIGN; case TK_MINUSEQ: return OP_SUBASSIGN;
    case TK_STAREQ:  return OP_MULASSIGN; case TK_SLASHEQ: return OP_DIVASSIGN;
    default: return OP_ADD;
    }
}

static int is_assign_tok(TokenKind k) {
    return k == TK_ASSIGN || k == TK_PLUSEQ || k == TK_MINUSEQ
        || k == TK_STAREQ  || k == TK_SLASHEQ;
}

static Expr *parse_precedence(P *p, Prec min);

static Expr *parse_primary(P *p) {
    const Token *t = peek(p);
    Loc loc = loc_of(t);
    switch (t->kind) {
    case TK_INT: {
        advance(p);
        Expr *e = ast_expr(p->arena, EX_INT, loc);
        e->int_val = t->int_val;
        return e;
    }
    case TK_FLOAT: {
        advance(p);
        Expr *e = ast_expr(p->arena, EX_FLOAT, loc);
        e->float_val = t->float_val;
        return e;
    }
    case TK_TRUE: case TK_FALSE: {
        advance(p);
        Expr *e = ast_expr(p->arena, EX_BOOL, loc);
        e->bool_val = (t->kind == TK_TRUE);
        return e;
    }
    case TK_STRING: {
        advance(p);
        Expr *e = ast_expr(p->arena, EX_STRING, loc);
        e->str_val = dup_string_lit(p, t);
        return e;
    }
    case TK_IDENT: {
        advance(p);
        const char *name = dup_ident(p, t);
        /* old(...) */
        if (strcmp(name, "old") == 0 && check(p, TK_LPAREN)) {
            advance(p);
            Expr *inner = parse_expr(p);
            expect(p, TK_RPAREN, "')'");
            Expr *e = ast_expr(p->arena, EX_OLD, loc);
            e->old_.inner = inner;
            return e;
        }
        /* struct literal: Name { f = v, ... } - only when next is { and it
         * smells like a struct literal. We accept Name { ident = ... }.
         * Suppressed inside if/while/for conditions so `if x { ... }` works. */
        if (!p->no_struct_lit
            && check(p, TK_LBRACE)
            && peek_n(p, 1)->kind == TK_IDENT
            && peek_n(p, 2)->kind == TK_ASSIGN)
        {
            advance(p); /* { */
            Expr *e = ast_expr(p->arena, EX_STRUCT_LIT, loc);
            e->struct_lit.type_name = name;
            while (!check(p, TK_RBRACE) && !check(p, TK_EOF)) {
                const Token *fn_t = expect(p, TK_IDENT, "field name");
                expect(p, TK_ASSIGN, "'='");
                Expr *v = parse_expr(p);
                StructField sf = { dup_ident(p, fn_t), v };
                vec_push(&e->struct_lit.fields, sf);
                if (!match(p, TK_COMMA)) break;
            }
            expect(p, TK_RBRACE, "'}'");
            return e;
        }
        Expr *e = ast_expr(p->arena, EX_IDENT, loc);
        e->ident = name;
        return e;
    }
    case TK_LPAREN: {
        advance(p);
        int saved = p->no_struct_lit;
        p->no_struct_lit = 0;          /* parens reset the suppression */
        Expr *e = parse_expr(p);
        p->no_struct_lit = saved;
        expect(p, TK_RPAREN, "')'");
        return e;
    }
    case TK_MINUS: case TK_BANG: case TK_STAR: case TK_AMP: case TK_TILDE: {
        advance(p);
        Expr *operand = parse_precedence(p, PREC_UNARY);
        Op op;
        switch (t->kind) {
        case TK_MINUS: op = OP_NEG;    break;
        case TK_BANG:  op = OP_NOT;    break;
        case TK_STAR:  op = OP_DEREF;  break;
        case TK_AMP:   op = OP_ADDR;   break;
        case TK_TILDE: op = OP_BITNOT; break;
        default: op = OP_NEG;
        }
        Expr *e = ast_expr(p->arena, EX_UNARY, loc);
        e->unary.op = op;
        e->unary.e = operand;
        return e;
    }
    default:
        error_at(p, t, "expected expression, got '%s'", token_kind_name(t->kind));
        advance(p);
        Expr *e = ast_expr(p->arena, EX_INT, loc);
        return e;
    }
}

static Expr *parse_call_or_postfix(P *p, Expr *base) {
    for (;;) {
        const Token *t = peek(p);
        Loc loc = loc_of(t);
        if (match(p, TK_LPAREN)) {
            Expr *call = ast_expr(p->arena, EX_CALL, loc);
            call->call.callee = base;
            if (!check(p, TK_RPAREN)) {
                for (;;) {
                    Expr *a = parse_expr(p);
                    vec_push(&call->call.args, a);
                    if (!match(p, TK_COMMA)) break;
                }
            }
            expect(p, TK_RPAREN, "')'");
            base = call;
        } else if (match(p, TK_LBRACK)) {
            Expr *e = ast_expr(p->arena, EX_INDEX, loc);
            e->index_.base = base;
            e->index_.index = parse_expr(p);
            expect(p, TK_RBRACK, "']'");
            base = e;
        } else if (match(p, TK_DOT)) {
            const Token *id = expect(p, TK_IDENT, "field name");
            Expr *e = ast_expr(p->arena, EX_FIELD, loc);
            e->field.base = base;
            e->field.name = dup_ident(p, id);
            base = e;
        } else {
            break;
        }
    }
    return base;
}

static Expr *parse_precedence(P *p, Prec min) {
    Expr *left = parse_primary(p);
    left = parse_call_or_postfix(p, left);

    for (;;) {
        TokenKind k = peek(p)->kind;
        Prec pr = op_precedence(k);
        if (pr == PREC_NONE || pr < min) break;
        if (pr == PREC_CALL) break; /* already handled */
        const Token *opt = advance(p);
        Loc loc = loc_of(opt);

        if (is_assign_tok(k)) {
            Expr *rhs = parse_precedence(p, PREC_ASSIGN);  /* right-assoc */
            Expr *e = ast_expr(p->arena, EX_ASSIGN, loc);
            e->assign.op  = tok_to_binop(k);
            e->assign.lhs = left;
            e->assign.rhs = rhs;
            left = e;
            continue;
        }
        if (k == TK_DOTDOT) {
            Expr *rhs = parse_precedence(p, (Prec)(pr + 1));
            Expr *e = ast_expr(p->arena, EX_RANGE, loc);
            e->range.lo = left;
            e->range.hi = rhs;
            left = e;
            continue;
        }
        Expr *rhs = parse_precedence(p, (Prec)(pr + 1));
        Expr *e = ast_expr(p->arena, EX_BINARY, loc);
        e->binary.op = tok_to_binop(k);
        e->binary.l  = left;
        e->binary.r  = rhs;
        left = e;
        left = parse_call_or_postfix(p, left);
    }
    return left;
}

static Expr *parse_expr(P *p) { return parse_precedence(p, PREC_ASSIGN); }

/* ----- statements ----- */

static int starts_let(P *p) {
    /* IDENT := ... or IDENT : type = ... */
    if (!check(p, TK_IDENT)) return 0;
    TokenKind k1 = peek_n(p, 1)->kind;
    if (k1 == TK_WALRUS) return 1;
    /* ident : type = ... → also a let. distinguish from ident : type as an
     * arg list - but we never parse top-level args as statements, so safe. */
    if (k1 == TK_COLON) return 1;
    return 0;
}

static Stmt *parse_let(P *p) {
    const Token *id = advance(p);
    Loc loc = loc_of(id);
    Stmt *s = ast_stmt(p->arena, ST_LET, loc);
    s->let_.name = dup_ident(p, id);
    if (match(p, TK_COLON)) {
        s->let_.type = parse_type(p);
        if (match(p, TK_ASSIGN)) s->let_.init = parse_expr(p);
    } else {
        expect(p, TK_WALRUS, "':=' or ':'");
        s->let_.init = parse_expr(p);
    }
    expect(p, TK_SEMI, "';'");
    return s;
}

static Stmt *parse_if(P *p) {
    Loc loc = loc_of(advance(p)); /* 'if' */
    Stmt *s = ast_stmt(p->arena, ST_IF, loc);
    p->no_struct_lit++;
    s->if_.cond = parse_expr(p);
    p->no_struct_lit--;
    parse_block(p, &s->if_.then_b);
    if (match(p, TK_ELSE)) {
        if (check(p, TK_IF)) {
            /* else if - wrap as a single-stmt block */
            Stmt *inner = parse_if(p);
            vec_push(&s->if_.else_b, inner);
        } else {
            parse_block(p, &s->if_.else_b);
        }
    }
    return s;
}

static Stmt *parse_while(P *p) {
    Loc loc = loc_of(advance(p));
    Stmt *s = ast_stmt(p->arena, ST_WHILE, loc);
    p->no_struct_lit++;
    s->while_.cond = parse_expr(p);
    p->no_struct_lit--;
    parse_block(p, &s->while_.body);
    return s;
}

static Stmt *parse_for(P *p) {
    Loc loc = loc_of(advance(p));
    const Token *v = expect(p, TK_IDENT, "loop variable");
    expect(p, TK_IN, "'in'");
    Stmt *s = ast_stmt(p->arena, ST_FOR, loc);
    s->for_.var = dup_ident(p, v);
    p->no_struct_lit++;
    s->for_.range = parse_expr(p);
    p->no_struct_lit--;
    parse_block(p, &s->for_.body);
    return s;
}

static Stmt *parse_match(P *p) {
    Loc loc = loc_of(advance(p));
    Stmt *s = ast_stmt(p->arena, ST_MATCH, loc);
    p->no_struct_lit++;
    s->match_.scrutinee = parse_expr(p);
    p->no_struct_lit--;
    expect(p, TK_LBRACE, "'{'");
    while (!check(p, TK_RBRACE) && !check(p, TK_EOF)) {
        MatchArm arm = {0};
        if (match(p, TK_UNDERSCORE)) arm.is_default = 1;
        else                          arm.pattern = parse_expr(p);
        expect(p, TK_FATARROW, "'=>'");
        if (check(p, TK_LBRACE)) {
            parse_block(p, &arm.body);
        } else {
            Stmt *one = parse_stmt(p);
            vec_push(&arm.body, one);
        }
        match(p, TK_COMMA);
        vec_push(&s->match_.arms, arm);
    }
    expect(p, TK_RBRACE, "'}'");
    return s;
}

static Stmt *parse_return(P *p) {
    Loc loc = loc_of(advance(p));
    Stmt *s = ast_stmt(p->arena, ST_RETURN, loc);
    if (!check(p, TK_SEMI) && !check(p, TK_EOF))
        s->expr = parse_expr(p);
    expect(p, TK_SEMI, "';'");
    return s;
}

static Stmt *parse_assert(P *p) {
    Loc loc = loc_of(advance(p));
    Stmt *s = ast_stmt(p->arena, ST_ASSERT, loc);
    expect(p, TK_LPAREN, "'('");
    s->expr = parse_expr(p);
    expect(p, TK_RPAREN, "')'");
    expect(p, TK_SEMI, "';'");
    return s;
}

static Stmt *parse_stmt(P *p) {
    switch (peek(p)->kind) {
    case TK_IF:     return parse_if(p);
    case TK_WHILE:  return parse_while(p);
    case TK_FOR:    return parse_for(p);
    case TK_MATCH:  return parse_match(p);
    case TK_RETURN: return parse_return(p);
    case TK_ASSERT: return parse_assert(p);
    case TK_LBRACE: {
        Stmt *s = ast_stmt(p->arena, ST_BLOCK, loc_of(peek(p)));
        parse_block(p, &s->block);
        return s;
    }
    default: break;
    }
    if (starts_let(p)) return parse_let(p);
    /* expression statement */
    Loc loc = loc_of(peek(p));
    Stmt *s = ast_stmt(p->arena, ST_EXPR, loc);
    s->expr = parse_expr(p);
    expect(p, TK_SEMI, "';'");
    return s;
}

static void parse_block(P *p, StmtVec *out) {
    expect(p, TK_LBRACE, "'{'");
    while (!check(p, TK_RBRACE) && !check(p, TK_EOF)) {
        Stmt *s = parse_stmt(p);
        vec_push(out, s);
    }
    expect(p, TK_RBRACE, "'}'");
}

/* ----- attributes & decls ----- */

static Attr *parse_attr(P *p) {
    Loc loc = loc_of(advance(p)); /* '@' */
    const Token *id = expect(p, TK_IDENT, "attribute name");
    Attr *a = ast_attr(p->arena, loc, dup_ident(p, id), NULL);
    if (match(p, TK_LPAREN)) {
        a->arg = parse_expr(p);
        expect(p, TK_RPAREN, "')'");
    }
    return a;
}

static void parse_attr_list(P *p, AttrVec *out) {
    while (check(p, TK_AT)) {
        Attr *a = parse_attr(p);
        vec_push(out, a);
    }
}

static Decl *parse_fn(P *p, AttrVec attrs) {
    Loc loc = loc_of(advance(p)); /* 'fn' */
    const Token *id = expect(p, TK_IDENT, "function name");
    Decl *d = ast_decl(p->arena, DK_FN, loc);
    d->attrs = attrs;
    d->fn.name = dup_ident(p, id);

    expect(p, TK_LPAREN, "'('");
    if (!check(p, TK_RPAREN)) {
        for (;;) {
            const Token *pn = expect(p, TK_IDENT, "param name");
            expect(p, TK_COLON, "':'");
            AstType *ty = parse_type(p);
            Param par = { dup_ident(p, pn), ty };
            vec_push(&d->fn.params, par);
            if (!match(p, TK_COMMA)) break;
        }
    }
    expect(p, TK_RPAREN, "')'");
    if (match(p, TK_ARROW)) d->fn.ret = parse_type(p);
    parse_block(p, &d->fn.body);
    return d;
}

static Decl *parse_struct(P *p, AttrVec attrs) {
    Loc loc = loc_of(advance(p)); /* 'struct' */
    const Token *id = expect(p, TK_IDENT, "struct name");
    Decl *d = ast_decl(p->arena, DK_STRUCT, loc);
    d->attrs = attrs;
    d->struc.name = dup_ident(p, id);

    expect(p, TK_LBRACE, "'{'");
    while (!check(p, TK_RBRACE) && !check(p, TK_EOF)) {
        if (check(p, TK_AT)) {
            /* attribute clause inside struct → invariant */
            AttrVec ats = {0};
            parse_attr_list(p, &ats);
            for (size_t i = 0; i < ats.len; i++)
                vec_push(&d->struc.invariants, ats.data[i]);
            vec_free(&ats);
            continue;
        }
        const Token *fn_tok = expect(p, TK_IDENT, "field name");
        expect(p, TK_COLON, "':'");
        AstType *ty = parse_type(p);
        StructFieldDecl f = { dup_ident(p, fn_tok), ty, {0} };
        vec_push(&d->struc.fields, f);
        match(p, TK_COMMA);
    }
    expect(p, TK_RBRACE, "'}'");
    return d;
}

static Decl *parse_type_alias(P *p, AttrVec attrs) {
    Loc loc = loc_of(advance(p)); /* 'type' */
    const Token *id = expect(p, TK_IDENT, "type name");
    expect(p, TK_ASSIGN, "'='");
    AstType *al = parse_type(p);
    Decl *d = ast_decl(p->arena, DK_TYPE_ALIAS, loc);
    d->attrs = attrs;
    d->alias.name = dup_ident(p, id);
    d->alias.aliased = al;
    return d;
}

static Decl *parse_decl(P *p, AttrVec attrs) {
    switch (peek(p)->kind) {
    case TK_FN:     return parse_fn(p, attrs);
    case TK_STRUCT: return parse_struct(p, attrs);
    case TK_TYPE:   return parse_type_alias(p, attrs);
    default:
        error_at(p, peek(p), "expected fn / struct / type, got '%s'",
                 token_kind_name(peek(p)->kind));
        advance(p);
        return NULL;
    }
}

/* ----- top level ----- */

Module *parse_module(Arena *arena, const TokenVec *tokens, const char *filename) {
    P p = { tokens, 0, arena, filename, 0, 0 };
    Module *m = (Module *)arena_alloc_zero(arena, sizeof(Module));

    if (match(&p, TK_MODULE)) {
        const Token *id = expect(&p, TK_IDENT, "module name");
        m->name = dup_ident(&p, id);
        match(&p, TK_SEMI);
    } else {
        m->name = "main";
    }

    while (match(&p, TK_IMPORT)) {
        const Token *id = expect(&p, TK_IDENT, "import name");
        Decl *d = ast_decl(arena, DK_IMPORT, loc_of(id));
        d->import_.name = dup_ident(&p, id);
        vec_push(&m->decls, d);
        match(&p, TK_SEMI);
    }

    while (!check(&p, TK_EOF)) {
        AttrVec attrs = {0};
        parse_attr_list(&p, &attrs);
        Decl *d = parse_decl(&p, attrs);
        if (d) vec_push(&m->decls, d);
    }

    return p.had_error ? NULL : m;
}
