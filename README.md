# (in)V(ari)ANTa -- vanta

A small statically-typed systems language with an attribute-driven
compilation model.

This is a hobby compiler. There is no codegen - there's a tree-walking
interpreter. See [IMPL.md](IMPL.md) for the language specification.

## What's interesting about it

- **Contracts that vanish in release.** `@requires` / `@ensures` /
  `@invariant` are gated by an attribute. Active in `--debug`, dropped
  in `--release` — no `#ifdef NDEBUG`, no per-callsite checks.

  ```c
  @debug @requires(s.size < s.capacity)
  @debug @ensures(s.size == old(s.size) + 1)
  fn push(s: *Stack, v: int) {
      s.data[s.size] = v;
      s.size += 1;
  }
  ```

  One body. The contract is part of the signature. `old(s.size)` is
  captured on entry so the postcondition can talk about pre-call
  state.

- **Function variants for when the bodies actually differ.** Same
  name, same signature, different implementations selected at compile
  time:

  ```c
  @debug
  fn log(msg: int) { println(msg); }

  @release
  fn log(msg: int) { /* gone */ }
  ```

  The variant resolver picks one at sema time based on `--attr`. The
  other is never lowered. C does this with macros and `#ifdef` piles.

- **No runtime cost when checks are off.** The lowering pass drops
  invariants whose gating attribute isn't active, so the interpreter
  never even sees them.

## Build

```
make
make test
```

C11. No dependencies.

## Try it

```
./vanta run examples/fib.vt              # exits with 55
./vanta run examples/sum.vt              # exits with 55
./vanta run --debug   examples/stack.vt  # contract-checked push
./vanta run --release examples/stack.vt  # bare push, no checks

./vanta run --debug   examples/stack_overflow.vt   # @requires fires
./vanta run --release examples/stack_overflow.vt   # plain OOB
```

## Subcommands

```
vanta lex      <file>     dump the token stream
vanta parse    <file>     pretty-print the AST
vanta check    <file>     parse + type-check
vanta variants <file>     show which variant is selected per fn
vanta run      <file>     execute main()
```

All commands accept `--attr NAME` (repeatable) plus `--debug` /
`--release` shortcuts.

## Layout

```
src/
  arena.{c,h}    bump allocator
  vec.h          dynamic-array macro
  token.{c,h}    token kinds + names
  lexer.{c,h}    hand-written, single-char lookahead
  ast.{c,h}      tagged-union nodes + tree printer
  parser.{c,h}   recursive descent + pratt
  type.{c,h}     resolved type representation
  sema.{c,h}     name resolution + type checking + variant collection
  lower.{c,h}    drop inactive invariants, gather active ones
  interp.{c,h}   tree-walking interpreter
  io.{c,h}       read_file
  main.c         cli
examples/        small programs
tests/run.sh     smoke runner
IMPL.md          spec
```

## Status

Reasonably complete given the spec. Plenty of TODOs in the code -
struct field memory leaks on scope exit, `alloc_array(T, n)` parses T
as an identifier and resolves it specially in sema (it should be a
proper type-arg form), error recovery in the parser is a stub, no
codegen.

