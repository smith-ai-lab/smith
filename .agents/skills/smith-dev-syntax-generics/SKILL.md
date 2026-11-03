---
name: smith-dev-syntax-generics
description: "New syntax (#1428): types as ordinary arguments (`fn alloc(T: type, n: usize) -> []T`, `mem.alloc(u8, 10)`), compile-time value parameters (`const n: usize`), inferred parameters in brackets (`fn sum[T: num](xs: []T) -> T:`) and pinning them at a call (`sum[f64](xs)`), concepts as predicates (`a: num`, `fn shape(T: type) -> bool`, `requires(s: *T): ...`), type functions returning an unnamed `struct:` or `enum:` (`fn Pair(T: type) -> type:`, `Option(T)`), aliases. Load when writing generic code in the new syntax."
---

# Generics, concepts and type functions

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428. Compile time and reflection are in [smith-dev-syntax-compile-time](../smith-dev-syntax-compile-time/SKILL.md).

## Types and compile-time values as arguments

```smith
fn alloc(T: type, n: usize) -> []T: ...         # mem.alloc(u8, 10): T is an ordinary argument
fn table(const n: usize) -> [n]f32: ...         # table(64): a compile-time value
```

A type is a parameter like any other, of type `type`; `const` marks a compile-time argument. Each distinct set of them is one instance; there is no run-time dispatch.

## Inferred parameters

```smith
fn push[T](xs: *mut List(T), x: T): ...         # T worked out from the arguments
fn sum[T: num](xs: []T) -> T: ...               # T restricted to a concept
fn first[T, n: usize](data: [n]T) -> T: data[0] # a type and a compile-time value
let total = sum[f64](xs)                        # brackets at a call pin T explicitly
```

- Brackets in a declaration are only for parameters worked out from the arguments: `T`, `T: concept` or `n: usize`. A parameter no argument can supply is an ordinary `T: type` or `const` parameter instead.

## Concepts

```smith
fn larger(a: num, b: typeof(a)) -> typeof(a):   # a concept stands where a type goes
    if a > b: a else: b
fn shape(T: type) -> bool:                      # a concept is a predicate: fn(T: type) -> bool
    requires(s: *T):                            # true when the block type-checks; never runs
        let _: f64 = s.area()                   # the function exists and returns exactly f64
static_assert(shape(Circle))                    # the convention, beside each concept
fn count[C](c: *C) -> usize:
    if requires(x: *C): let _: usize = x.len()  # a compile-time bool anywhere: only the taken branch is checked
        c.len()
    else: sum(for _ in c: 1)
```

Built-in concepts are lowercase (`int`, `float`, `num`, `pointer`, ...); user ones combine with `and`, `or`, `not`. A false concept names the false part and the first type error inside `requires`. There are no traits, no `concept` declaration and no checking of a generic's body against its concept. A struct field and a function exported to C need concrete types.

## Type functions

```smith
fn Pair(T: type) -> type:
    struct:                                     # the one unnamed struct; it takes the name Pair(T)
        first: T
        second: T
let IntPair = Pair(i32)                         # an alias; Option(T) returns an unnamed `enum:` the same way
let p = IntPair(1, 2)                           # or Pair(i32)(first = 1, second = 2)
```

## Not settled yet

A computed result type (`-> leaf_of(T)`); which module a std generic's instance counts as for private fields.
