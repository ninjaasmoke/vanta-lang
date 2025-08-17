/*
 * io.h — file I/O helpers (just one for now: read a whole file).
 */

#ifndef VANTA_IO_H
#define VANTA_IO_H

#include <stddef.h>

/* Reads the whole file into a malloc'd, NUL-terminated buffer.
 * Returns NULL on failure (and prints a message). Caller frees. */
char *read_file(const char *path, size_t *out_len);

#endif
