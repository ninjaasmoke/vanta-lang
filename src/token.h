/*
 * token.h - token kinds + the Token struct.
 */

#ifndef VANTA_TOKEN_H
#define VANTA_TOKEN_H

#include <stddef.h>

typedef enum {
    TK_EOF = 0,
    TK_ERROR,

    /* literals */
    TK_IDENT,
    TK_INT,
    TK_FLOAT,
    TK_STRING,

    /* keywords */
    TK_MODULE, TK_IMPORT, TK_FN, TK_STRUCT, TK_TYPE, TK_RETURN,
    TK_IF, TK_ELSE, TK_WHILE, TK_FOR, TK_IN, TK_MATCH,
    TK_TRUE, TK_FALSE, TK_ASSERT,

    /* punctuation */
    TK_LPAREN, TK_RPAREN,         /* ( ) */
    TK_LBRACE, TK_RBRACE,         /* { } */
    TK_LBRACK, TK_RBRACK,         /* [ ] */
    TK_COMMA, TK_DOT, TK_COLON,
    TK_SEMI,                      /* ; (optional) */
    TK_AT,                        /* @ */
    TK_ARROW,                     /* -> */
    TK_FATARROW,                  /* => */
    TK_DOTDOT,                    /* .. */
    TK_UNDERSCORE,                /* _ */

    /* operators */
    TK_ASSIGN,        /* =  */
    TK_WALRUS,        /* := */
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
    TK_AMP,           /* &  (address-of OR bitwise-and depending on context) */
    TK_PIPE,          /* |  bitwise or */
    TK_CARET,         /* ^  bitwise xor */
    TK_TILDE,         /* ~  bitwise not */
    TK_SHL, TK_SHR,   /* << >> */
    TK_BANG,          /* !  */
    TK_EQ, TK_NEQ, TK_LT, TK_GT, TK_LE, TK_GE,
    TK_AND, TK_OR,    /* && || */
    TK_PLUSEQ, TK_MINUSEQ, TK_STAREQ, TK_SLASHEQ,

    /* control-flow keywords (post-MVP) */
    TK_BREAK, TK_CONTINUE,

    TK__COUNT
} TokenKind;

typedef struct {
    TokenKind kind;
    const char *start;   /* points into source buffer */
    size_t      len;
    int         line;
    int         col;

    /* parsed literal payloads (only valid for TK_INT/TK_FLOAT) */
    long long   int_val;
    double      float_val;
} Token;

const char *token_kind_name(TokenKind k);

#endif
