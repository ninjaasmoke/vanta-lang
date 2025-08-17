/*
 * vanta — entry point.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "lexer.h"
#include "token.h"

#define VANTA_VERSION "0.0.1"

static int usage(void) {
    fputs(
        "vanta " VANTA_VERSION "\n"
        "usage: vanta <command> [args]\n"
        "\n"
        "commands:\n"
        "  version          print version\n"
        "  lex <file>       dump the token stream of <file>\n",
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

int main(int argc, char **argv) {
    if (argc < 2) return usage();
    if (strcmp(argv[1], "version") == 0) {
        printf("vanta %s\n", VANTA_VERSION);
        return 0;
    }
    if (strcmp(argv[1], "lex") == 0) {
        return cmd_lex(argc - 2, argv + 2);
    }
    return usage();
}
