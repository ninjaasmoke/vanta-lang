/*
 * vanta — entry point.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "lexer.h"
#include "token.h"
#include "parser.h"
#include "ast.h"
#include "arena.h"
#include "sema.h"
#include "vec.h"

#define VANTA_VERSION "0.0.1"

static int usage(void) {
    fputs(
        "vanta " VANTA_VERSION "\n"
        "usage: vanta <command> [args]\n"
        "\n"
        "commands:\n"
        "  version          print version\n"
        "  lex <file>       dump the token stream of <file>\n"
        "  parse <file>     parse and pretty-print the AST\n"
        "  check <file>     parse and type-check (--attr NAME ...)\n",
        stderr);
    return 1;
}

static int cmd_lex(int argc, char **argv) {
    if (argc < 1) {
        fputs("lex: missing <file>\n", stderr);
        return 1;
    }
    const char *path = argv[0];
    size_t n = 0;
    char *src = read_file(path, &n);
    if (!src) return 1;

    TokenVec toks = {0};
    lexer_lex_all(src, path, &toks);
    for (size_t i = 0; i < toks.len; i++) {
        Token t = toks.data[i];
        printf("%4d:%-3d  %-10s  %.*s\n",
               t.line, t.col, token_kind_name(t.kind),
               (int)t.len, t.start);
    }
    vec_free(&toks);
    free(src);
    return 0;
}

static int cmd_parse(int argc, char **argv) {
    if (argc < 1) { fputs("parse: missing <file>\n", stderr); return 1; }
    const char *path = argv[0];
    size_t n = 0;
    char *src = read_file(path, &n);
    if (!src) return 1;

    TokenVec toks = {0};
    lexer_lex_all(src, path, &toks);

    Arena a; arena_init(&a, 1 << 16);
    Module *m = parse_module(&a, &toks, path);
    int rc = 0;
    if (!m) { rc = 1; }
    else    { ast_print_module(m); }

    arena_free(&a);
    vec_free(&toks);
    free(src);
    return rc;
}

/* parse "--attr NAME --attr NAME2 <file>" — accumulate attrs and return file. */
typedef VEC(const char *) StrVec;

static const char *parse_attr_args(int argc, char **argv, StrVec *out) {
    const char *file = NULL;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--attr") == 0 && i + 1 < argc) {
            vec_push(out, (const char *)argv[i + 1]);
            i++;
        } else {
            file = argv[i];
        }
    }
    return file;
}

static int cmd_check(int argc, char **argv) {
    StrVec attrs = {0};
    const char *path = parse_attr_args(argc, argv, &attrs);
    if (!path) { fputs("check: missing <file>\n", stderr); vec_free(&attrs); return 1; }

    size_t n = 0;
    char *src = read_file(path, &n);
    if (!src) { vec_free(&attrs); return 1; }

    TokenVec toks = {0};
    lexer_lex_all(src, path, &toks);

    Arena a; arena_init(&a, 1 << 16);
    Module *m = parse_module(&a, &toks, path);
    int rc = 0;
    if (!m) {
        rc = 1;
    } else {
        AttrSet active = attrset_make(&a, attrs.data, attrs.len);
        SemaProgram *prog = sema_analyze(&a, m, &active);
        if (prog->had_error) rc = 1;
        else fputs("ok\n", stdout);
    }
    arena_free(&a);
    vec_free(&toks);
    vec_free(&attrs);
    free(src);
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 2) return usage();
    if (strcmp(argv[1], "version") == 0) {
        printf("vanta %s\n", VANTA_VERSION);
        return 0;
    }
    if (strcmp(argv[1], "lex") == 0) {
        return cmd_lex(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "parse") == 0) {
        return cmd_parse(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "check") == 0) {
        return cmd_check(argc - 2, argv + 2);
    }
    return usage();
}
