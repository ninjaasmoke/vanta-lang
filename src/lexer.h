/*
 * lexer.h - turns a source buffer into a stream of tokens.
 *
 * The lexer does not own the source. It expects a NUL-terminated
 * buffer that lives at least as long as the produced tokens (since
 * Token.start points into it).
 */

#ifndef VANTA_LEXER_H
#define VANTA_LEXER_H

#include "token.h"
#include "vec.h"

typedef VEC(Token) TokenVec;

typedef struct {
    const char *src;       /* full source */
    const char *p;         /* current cursor */
    const char *filename;  /* for diagnostics */
    int         line;
    int         col;
    int         had_error;
} Lexer;

void lexer_init(Lexer *L, const char *src, const char *filename);
Token lexer_next(Lexer *L);
void lexer_lex_all(const char *src, const char *filename, TokenVec *out);

#endif
