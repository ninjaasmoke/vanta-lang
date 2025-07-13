# vanta

A small statically-typed systems language with an attribute-driven
compilation model.

This is a hobby compiler. It is not finished. See [IMPL.md](IMPL.md)
for the language specification.

## Goals

- Explicit memory, no GC
- Attribute-gated function variants (one variant emitted per build)
- Invariants (`@requires` / `@ensures` / `@invariant`) that compile away
  to nothing when their gating attribute is not active
- A tiny, readable codebase

## Status

Pre-alpha. The lexer is the only thing close to working.

## Build

```
make
```

Requires a C11 compiler.
