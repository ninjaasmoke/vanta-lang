/*
 * interp.h - tree-walking interpreter for vanta.
 *
 * Runs a sema'd + lowered program. Honors active invariants at call
 * boundaries. Aborts the program (with a message) on assertion or
 * invariant failure.
 */

#ifndef VANTA_INTERP_H
#define VANTA_INTERP_H

#include "lower.h"

/* Run main(). Returns the integer return value of main, or:
 *   1   if main is missing
 *   2   if an invariant / assert fired
 */
int interp_run(LoweredProgram *L);

#endif
