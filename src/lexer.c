#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * Tiny hand-written lexer. No regex, no tables — just a switch.
 * Tokens point back into the source buffer, so we never copy strings
 * here; the parser will arena-dup any identifier it actually keeps.
 */

void lexer_init(Lexer *L, const char *src, const char *filename) {
    L->src = src;
    L->p = src;
    L->filename = filename ? filename : "<input>";
    L->line = 1;
    L->col = 1;
    L->had_error = 0;
}

static int peek(const Lexer *L)       { return (unsigned char)*L->p; }
static int peek_at(const Lexer *L, int o) { return (unsigned char)L->p[o]; }

static int advance(Lexer *L) {
    int c = (unsigned char)*L->p;
    if (c == '\0') return c;
    L->p++;
    if (c == '\n') { L->line++; L->col = 1; }
    else            { L->col++;             }
    return c;
}

static int match(Lexer *L, char c) {
    if (peek(L) == c) { advance(L); return 1; }
    return 0;
}

static void skip_trivia(Lexer *L) {
    for (;;) {
        int c = peek(L);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(L);
        } else if (c == '/' && peek_at(L, 1) == '/') {
            while (peek(L) && peek(L) != '\n') advance(L);
        } else if (c == '/' && peek_at(L, 1) == '*') {
            advance(L); advance(L);
            while (peek(L) && !(peek(L) == '*' && peek_at(L, 1) == '/')) advance(L);
            if (peek(L)) { advance(L); advance(L); }
        } else {
            break;
        }
    }
}

static Token make(Lexer *L, TokenKind k, const char *start, int line, int col) {
    Token t;
    t.kind = k;
    t.start = start;
    t.len = (size_t)(L->p - start);
    t.line = line;
    t.col = col;
    t.int_val = 0;
    t.float_val = 0.0;
    return t;
}

static Token make_err(Lexer *L, const char *msg, const char *start, int line, int col) {
    fprintf(stderr, "%s:%d:%d: lex error: %s\n", L->filename, line, col, msg);
    L->had_error = 1;
    return make(L, TK_ERROR, start, line, col);
}

/* --- keyword table --- */
static struct { const char *s; TokenKind k; } kws[] = {
    { "module", TK_MODULE }, { "import", TK_IMPORT },
    { "fn",     TK_FN     }, { "struct", TK_STRUCT },
    { "type",   TK_TYPE   }, { "return", TK_RETURN },
    { "if",     TK_IF     }, { "else",   TK_ELSE   },
    { "while",  TK_WHILE  }, { "for",    TK_FOR    },
    { "in",     TK_IN     }, { "match",  TK_MATCH  },
    { "true",   TK_TRUE   }, { "false",  TK_FALSE  },
    { "assert", TK_ASSERT },
    { NULL, 0 }
};

static TokenKind ident_kind(const char *s, size_t n) {
    for (int i = 0; kws[i].s; i++) {
        if (strlen(kws[i].s) == n && memcmp(kws[i].s, s, n) == 0)
            return kws[i].k;
    }
    if (n == 1 && s[0] == '_') return TK_UNDERSCORE;
    return TK_IDENT;
}

static Token lex_ident(Lexer *L, const char *start, int line, int col) {
    while (isalnum((unsigned char)peek(L)) || peek(L) == '_') advance(L);
    Token t = make(L, TK_IDENT, start, line, col);
    t.kind = ident_kind(start, t.len);
    return t;
}

static Token lex_number(Lexer *L, const char *start, int line, int col) {
    int is_float = 0;
    /* hex / binary integer literals */
    if (start[0] == '0' && (peek(L) == 'x' || peek(L) == 'X')) {
        advance(L);
        while (isxdigit((unsigned char)peek(L)) || peek(L) == '_') advance(L);
        Token t = make(L, TK_INT, start, line, col);
        t.int_val = strtoll(start + 2, NULL, 16);
        return t;
    }
    if (start[0] == '0' && (peek(L) == 'b' || peek(L) == 'B')) {
        advance(L);
        while (peek(L) == '0' || peek(L) == '1' || peek(L) == '_') advance(L);
        Token t = make(L, TK_INT, start, line, col);
        t.int_val = strtoll(start + 2, NULL, 2);
        return t;
    }
    while (isdigit((unsigned char)peek(L))) advance(L);
    if (peek(L) == '.' && isdigit((unsigned char)peek_at(L, 1))) {
        is_float = 1;
        advance(L);
        while (isdigit((unsigned char)peek(L))) advance(L);
    }
    Token t = make(L, is_float ? TK_FLOAT : TK_INT, start, line, col);
    /* parse the value into the payload */
    char buf[64];
    size_t n = t.len < sizeof(buf) - 1 ? t.len : sizeof(buf) - 1;
    memcpy(buf, start, n);
    buf[n] = '\0';
    if (is_float) t.float_val = strtod(buf, NULL);
    else          t.int_val   = strtoll(buf, NULL, 10);
    return t;
}

static Token lex_string(Lexer *L, const char *start, int line, int col) {
    /* opening quote already consumed */
    while (peek(L) && peek(L) != '"') {
        if (peek(L) == '\\' && peek_at(L, 1)) advance(L);
        advance(L);
    }
    if (peek(L) != '"')
        return make_err(L, "unterminated string", start, line, col);
    advance(L); /* closing quote */
    return make(L, TK_STRING, start, line, col);
}

Token lexer_next(Lexer *L) {
    skip_trivia(L);
    int line = L->line, col = L->col;
    const char *start = L->p;
    int c = advance(L);

    if (c == 0) return make(L, TK_EOF, start, line, col);

    if (isalpha(c) || c == '_') return lex_ident(L, start, line, col);
    if (isdigit(c))             return lex_number(L, start, line, col);

    switch (c) {
    case '(': return make(L, TK_LPAREN, start, line, col);
    case ')': return make(L, TK_RPAREN, start, line, col);
    case '{': return make(L, TK_LBRACE, start, line, col);
    case '}': return make(L, TK_RBRACE, start, line, col);
    case '[': return make(L, TK_LBRACK, start, line, col);
    case ']': return make(L, TK_RBRACK, start, line, col);
    case ',': return make(L, TK_COMMA,  start, line, col);
    case ';': return make(L, TK_SEMI,   start, line, col);
    case '@': return make(L, TK_AT,     start, line, col);
    case '+': if (match(L, '=')) return make(L, TK_PLUSEQ,  start, line, col);
              return make(L, TK_PLUS,    start, line, col);
    case '-': if (match(L, '>')) return make(L, TK_ARROW,   start, line, col);
              if (match(L, '=')) return make(L, TK_MINUSEQ, start, line, col);
              return make(L, TK_MINUS,   start, line, col);
    case '*': if (match(L, '=')) return make(L, TK_STAREQ,  start, line, col);
              return make(L, TK_STAR,    start, line, col);
    case '/': if (match(L, '=')) return make(L, TK_SLASHEQ, start, line, col);
              return make(L, TK_SLASH,   start, line, col);
    case '%': return make(L, TK_PERCENT, start, line, col);
    case '.': if (match(L, '.')) return make(L, TK_DOTDOT,  start, line, col);
              return make(L, TK_DOT,     start, line, col);
    case ':': if (match(L, '=')) return make(L, TK_WALRUS,  start, line, col);
              return make(L, TK_COLON,   start, line, col);
    case '=': if (match(L, '=')) return make(L, TK_EQ,      start, line, col);
              if (match(L, '>')) return make(L, TK_FATARROW,start, line, col);
              return make(L, TK_ASSIGN,  start, line, col);
    case '!': if (match(L, '=')) return make(L, TK_NEQ,     start, line, col);
              return make(L, TK_BANG,    start, line, col);
    case '<': if (match(L, '=')) return make(L, TK_LE,      start, line, col);
              return make(L, TK_LT,      start, line, col);
    case '>': if (match(L, '=')) return make(L, TK_GE,      start, line, col);
              return make(L, TK_GT,      start, line, col);
    case '&': if (match(L, '&')) return make(L, TK_AND,     start, line, col);
              return make(L, TK_AMP,     start, line, col);
    case '|': if (match(L, '|')) return make(L, TK_OR,      start, line, col);
              return make_err(L, "stray '|'", start, line, col);
    case '"': return lex_string(L, start, line, col);
    }
    return make_err(L, "unexpected character", start, line, col);
}

void lexer_lex_all(const char *src, const char *filename, TokenVec *out) {
    Lexer L; lexer_init(&L, src, filename);
    for (;;) {
        Token t = lexer_next(&L);
        vec_push(out, t);
        if (t.kind == TK_EOF) break;
    }
}
