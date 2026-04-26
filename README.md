# vanta

A small statically-typed systems language with an attribute-driven
compilation model.

This is a hobby compiler. There is no codegen — there's a tree-walking
interpreter. See [IMPL.md](IMPL.md) for the language specification.

## What's interesting about it

- Functions can have multiple variants gated by attributes:

  ```c
  @debug
  @requires(s.size < s.capacity)
  @ensures(s.size == old(s.size) + 1)
  fn push(s: *Stack, v: int) {
      s.data[s.size] = v
      s.size += 1
  }

  @release
  fn push(s: *Stack, v: int) {
      s.data[s.size] = v
      s.size += 1
  }
  ```

  Build with `--attr debug` and the contract-checked variant runs.
  Build with `--attr release` and the bare one runs. The two are
  selected at sema time, not at runtime.

- Invariants (`@requires` / `@ensures` / `@invariant`) are zero-cost
  when their gating attribute isn't active — the lowering pass drops
  them before the interpreter ever sees them.

- `old(expr)` is captured on entry to a function so postconditions
  can reference pre-call state.

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

Reasonably complete given the spec. Plenty of TODOs in the code —
struct field memory leaks on scope exit, `alloc_array(T, n)` parses T
as an identifier and resolves it specially in sema (it should be a
proper type-arg form), error recovery in the parser is a stub, no
codegen.

