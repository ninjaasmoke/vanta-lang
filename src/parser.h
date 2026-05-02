/*
 * parser.h - turns a TokenVec into a Module.
 */

#ifndef VANTA_PARSER_H
#define VANTA_PARSER_H

#include "ast.h"
#include "lexer.h"

/* Parses the token stream. Returns NULL on error.
 * The returned Module and everything it points to is allocated in `arena`. */
Module *parse_module(Arena *arena, const TokenVec *tokens, const char *filename);

#endif
