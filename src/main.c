/*
 * vanta — entry point.
 *
 * For now this just prints a banner. The pieces (lexer, parser, sema,
 * interp) will arrive over time.
 */

#include <stdio.h>
#include <string.h>

#define VANTA_VERSION "0.0.1"

static int usage(void) {
    fputs(
        "vanta " VANTA_VERSION "\n"
        "usage: vanta <command> [args]\n"
        "\n"
        "commands:\n"
        "  version    print version\n",
        stderr);
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) return usage();
    if (strcmp(argv[1], "version") == 0) {
        printf("vanta %s\n", VANTA_VERSION);
        return 0;
    }
    return usage();
}
