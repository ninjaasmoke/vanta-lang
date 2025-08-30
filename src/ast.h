/*
 * ast.h — abstract syntax tree.
 *
 * One big tagged-union per category (Expr, Stmt, Decl). Allocated in
 * an Arena that the caller passes in. Nodes carry a source location
 * (line/col) for diagnostics.
 *
 * Types are represented by AstType — a small recursive descriptor
 * (primitive / pointer / array / slice / named). Sema turns this into
 * a resolved Type.
 */

#ifndef VANTA_AST_H
#define VANTA_AST_H

#include <stddef.h>
#include "arena.h"
#include "vec.h"

typedef struct AstType   AstType;
typedef struct Expr      Expr;
typedef struct Stmt      Stmt;
typedef struct Decl      Decl;
typedef struct Attr      Attr;
typedef struct Module    Module;

typedef VEC(Expr*) ExprVec;
typedef VEC(Stmt*) StmtVec;
typedef VEC(Decl*) DeclVec;
typedef VEC(Attr*) AttrVec;

typedef struct {
    int line;
    int col;
} Loc;

/* ---------- types ---------- */
typedef enum {
    AT_PRIM,      /* int, i32, bool, ... */
    AT_PTR,       /* *T  */
    AT_ARRAY,     /* [N]T */
    AT_SLICE,     /* []T  */
    AT_NAMED      /* user struct / type alias */
} AstTypeKind;

struct AstType {
    AstTypeKind kind;
    Loc loc;
    /* AT_PRIM, AT_NAMED */
    const char *name;
    /* AT_PTR / AT_ARRAY / AT_SLICE */
    AstType *elem;
    /* AT_ARRAY */
    long long array_len;
};

/* ---------- expressions ---------- */
typedef enum {
    EX_INT, EX_FLOAT, EX_BOOL, EX_STRING,
    EX_IDENT,
    EX_UNARY,        /* ! - * & */
    EX_BINARY,
    EX_CALL,
    EX_INDEX,        /* a[i] */
    EX_FIELD,        /* a.f  */
    EX_ASSIGN,       /* lhs = rhs (incl. += etc) */
    EX_RANGE,        /* a..b */
    EX_STRUCT_LIT,   /* T { f = v, ... } */
    EX_OLD           /* old(expr), only valid in ensures */
} ExprKind;

typedef enum {
    OP_NEG, OP_NOT, OP_DEREF, OP_ADDR,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_AND, OP_OR,
    OP_ASSIGN, OP_ADDASSIGN, OP_SUBASSIGN, OP_MULASSIGN, OP_DIVASSIGN
} Op;

typedef struct { const char *name; Expr *value; } StructField;
typedef VEC(StructField) StructFieldVec;

struct Expr {
    ExprKind kind;
    Loc loc;
    AstType *type_hint;   /* unused for most, used by struct-lit */
    union {
        long long       int_val;
        double          float_val;
        int             bool_val;
        const char     *str_val;
        const char     *ident;
        struct { Op op; Expr *e; }                unary;
        struct { Op op; Expr *l; Expr *r; }       binary;
        struct { Expr *callee; ExprVec args; }    call;
        struct { Expr *base; Expr *index; }       index_;
        struct { Expr *base; const char *name; }  field;
        struct { Op op; Expr *lhs; Expr *rhs; }   assign;
        struct { Expr *lo; Expr *hi; }            range;
        struct { const char *type_name; StructFieldVec fields; } struct_lit;
        struct { Expr *inner; }                   old_;
    };
};

/* ---------- statements ---------- */
typedef enum {
    ST_EXPR,         /* expression statement */
    ST_LET,          /* x := expr */
    ST_BLOCK,
    ST_IF,
    ST_WHILE,
    ST_FOR,          /* for x in lo..hi { } */
    ST_RETURN,
    ST_ASSERT,
    ST_MATCH
} StmtKind;

typedef struct { Expr *pattern; StmtVec body; int is_default; } MatchArm;
typedef VEC(MatchArm) MatchArmVec;

struct Stmt {
    StmtKind kind;
    Loc loc;
    union {
        Expr *expr;                                       /* ST_EXPR, ST_RETURN, ST_ASSERT */
        struct { const char *name; AstType *type;
                 Expr *init; }                let_;
        StmtVec block;
        struct { Expr *cond; StmtVec then_b; StmtVec else_b; } if_;
        struct { Expr *cond; StmtVec body; }              while_;
        struct { const char *var; Expr *range; StmtVec body; } for_;
        struct { Expr *scrutinee; MatchArmVec arms; }     match_;
    };
};

/* ---------- attributes ---------- */
struct Attr {
    Loc loc;
    const char *name;       /* "debug", "requires", "ensures", ... */
    Expr *arg;              /* nullable */
};

/* ---------- declarations ---------- */
typedef enum {
    DK_FN,
    DK_STRUCT,
    DK_TYPE_ALIAS,
    DK_IMPORT
} DeclKind;

typedef struct { const char *name; AstType *type; } Param;
typedef VEC(Param) ParamVec;

typedef struct {
    const char *name;
    AstType    *type;
    AttrVec     attrs;        /* @invariant on a field-set is on the struct */
} StructFieldDecl;
typedef VEC(StructFieldDecl) StructFieldDeclVec;

struct Decl {
    DeclKind kind;
    Loc loc;
    AttrVec attrs;
    union {
        struct {
            const char *name;
            ParamVec    params;
            AstType    *ret;        /* may be NULL → void */
            StmtVec     body;
        } fn;
        struct {
            const char *name;
            StructFieldDeclVec fields;
            AttrVec invariants;     /* @invariant clauses inside struct body */
        } struc;
        struct {
            const char *name;
            AstType    *aliased;
        } alias;
        struct {
            const char *name;
        } import_;
    };
};

/* ---------- module ---------- */
struct Module {
    const char *name;
    DeclVec decls;
};

/* helpers — all allocate from arena */
Expr   *ast_expr(Arena *a, ExprKind k, Loc loc);
Stmt   *ast_stmt(Arena *a, StmtKind k, Loc loc);
Decl   *ast_decl(Arena *a, DeclKind k, Loc loc);
Attr   *ast_attr(Arena *a, Loc loc, const char *name, Expr *arg);
AstType*ast_type(Arena *a, AstTypeKind k, Loc loc);

/* debug printer */
void ast_print_module(const Module *m);

#endif
