# Vanta Language Specification (v0.1)

---

# 1. Overview

Vanta is a statically typed, compiled systems programming language with:

* Explicit memory control
* No hidden runtime behavior
* Attribute-driven compilation model
* Zero-cost abstractions (invariants removable)
* LLVM backend

---

# 2. Compilation Model

## 2.1 Attribute Set

Compilation is parameterized by a set of active attributes:

```
vanta build --attr <name> ...
```

Example:

```
vanta build --attr debug --attr windows
```

Active attribute set:

```
A = { debug, windows }
```

---

## 2.2 Attribute Semantics

* Attributes are compile-time only
* No runtime representation
* Used for:

  * Function variant selection
  * Conditional invariants
  * Optional constraints

---

# 3. Lexical Structure

## 3.1 Identifiers

```
[a-zA-Z_][a-zA-Z0-9_]*
```

Case-sensitive.

---

## 3.2 Keywords

```
module import fn struct type return
if else while for match
true false
```

---

## 3.3 Literals

* Integer: `123`
* Float: `1.23`
* Boolean: `true`, `false`

---

# 4. Program Structure

## 4.1 Module

```c
module main
```

Single module per file (MVP).

---

## 4.2 Import

```c
import math
```

(No namespace system in MVP)

---

## 4.3 Statements

Statements are terminated by `;`. There is no significant whitespace.
Block-shaped constructs (`if`, `while`, `for`, `match`, function bodies)
are delimited by `{` and `}` and do not need a trailing `;`.

```c
x := 10;
y := add(x, 1);
return y;
```

---

# 5. Types

## 5.1 Primitive Types

```
int
i32 i64
u32 u64
u8        // one byte; what 'c' character literals produce
f32 f64
bool
void
```

---

## 5.2 Pointer Types

```
*T
```

Operations:

```c
*ptr        // dereference
&x          // address-of
```

---

## 5.3 Arrays

```
[N]T     // fixed-size
[]T      // slice
```

Slice layout:

```c
struct Slice<T> {
    data: *T
    len: int
}
```

---

## 5.4 Structs

```c
struct S {
    field: T
}
```

Rules:

* Plain memory layout
* No inheritance
* No hidden fields
* No constructors/destructors

---

## 5.5 Type Alias

```c
type Name = ExistingType
```

---

# 6. Expressions

## 6.1 Variables

```c
x := 10
```

---

## 6.2 Assignment

```c
x = 20
```

---

## 6.3 Arithmetic

```
+ - * / %
```

---

## 6.4 Comparison

```
== != < > <= >=
```

---

## 6.5 Logical

```
&& || !
```

---

# 7. Control Flow

## 7.1 If

```c
if cond {
} else {
}
```

---

## 7.2 While

```c
while cond {
}
```

---

## 7.3 For

```c
for i in 0..n {
}
```

---

## 7.4 Match

```c
match x {
    0 => {}
    _ => {}
}
```

---

# 8. Functions

## 8.1 Definition

```c
fn name(arg: T, ...) -> R {
    return value
}
```

---

## 8.2 Rules

* No overloading (except attribute variants)
* No default arguments
* Explicit return types required

---

# 9. Attribute System

## 9.1 Syntax

```c
@attr
@attr(expr)
```

Applies to:

* Functions
* Structs (invariants)
* Types (constraints)

---

## 9.2 Attribute Declaration (optional)

```c
@attribute(name)
```

Used for validation only.

---

# 10. Function Variant Dispatch

## 10.1 Definition

Multiple variants allowed:

```c
@a
fn f() {}

@a @b
fn f() {}

fn f() {}
```

---

## 10.2 Resolution

Given active set `A`:

1. Collect all variants
2. Keep variants where:

   ```
   V ⊆ A
   ```
3. Score = |V|
4. Select highest score

---

## 10.3 Errors

* Tie → compile-time error
* No match + no fallback → error

---

# 11. Invariants

All invariants are attributes.

---

## 11.1 Preconditions

```c
@requires(expr)
fn f(...) {}
```

---

## 11.2 Postconditions

```c
@ensures(expr)
fn f(...) {}
```

---

## 11.3 Struct Invariants

```c
struct S {
    x: int

    @invariant(x >= 0)
}
```

---

## 11.4 Assertions

```c
assert(expr)
```

---

## 11.5 Old Values

```c
old(expr)
```

Rules:

* Only valid in `@ensures`
* Compile-time error otherwise

---

## 11.6 Conditional Invariants

```c
@debug @requires(x > 0)
```

Active only if `debug ∈ A`.

---

# 12. Invariant Execution Model

## 12.1 Lowering (when active)

* `@requires` → function entry check
* `@ensures` → before return
* `@invariant` → after mutation points
* `assert` → inline check

---

## 12.2 Removal

If attribute not in `A`:

* Entire check removed
* No IR generated

---

## 12.3 Old Value Lowering

In active mode:

```c
tmp = expr
```

Used in postcondition.

Removed otherwise.

---

# 13. Memory Model

## 13.1 Allocation

```c
ptr = alloc(T)
free(ptr)
```

---

## 13.2 Array Allocation

```c
ptr = alloc_array(T, n)
```

---

## 13.3 Stack Allocation

```c
x := value
```

---

## 13.4 Rules

* No garbage collection
* No implicit allocation
* No automatic destruction
* No ownership system (MVP)

---

# 14. Type Constraints (Optional)

```c
type PositiveInt = int

@constraint(value > 0)
```

Behavior:

* Active attribute → check inserted
* Otherwise ignored

---

# 15. Error Handling

No exceptions.

User-defined patterns:

```c
struct Result<T> {
    ok: bool
    value: T
}
```

---

# 16. Compiler Semantics

## 16.1 Attribute Resolution

* Per-function resolution
* Happens before code generation

---

## 16.2 IR Requirements

* Only selected variant emitted
* No unused variants in final IR

---

## 16.3 Invariant Handling

* Inserted during lowering phase
* Removed if inactive

---

# 17. Undefined Behavior

The following are undefined:

* Dereferencing null or invalid pointers
* Out-of-bounds memory access
* Use-after-free
* Double free
* Integer overflow (implementation-defined)

---

# 18. Restrictions (MVP)

Not supported:

* Generics
* Macros
* Exceptions
* Operator overloading
* Reflection
* Namespaces
* Concurrency primitives

---

# 19. Example

```c
module main

struct Stack {
    data: *int
    size: int
    capacity: int

    @debug @invariant(size <= capacity)
}

@debug
@requires(s != null)
@requires(s.size < s.capacity)
@ensures(s.size == old(s.size) + 1)
fn push(s: *Stack, v: int) {
    s.data[s.size] = v;
    s.size += 1;
}

@release
fn push(s: *Stack, v: int) {
    s.data[s.size] = v;
    s.size += 1;
}

fn main() -> int {
    s := Stack {
        data = alloc_array(int, 10),
        size = 0,
        capacity = 10
    };

    push(&s, 42);

    free(s.data);

    return 0;
}
```

---

# 20. Core Guarantees

* Single implementation selected per function
* No runtime branching from attributes
* Invariants are zero-cost when inactive
* Memory behavior is fully explicit
* Compilation is deterministic given attribute set

---
