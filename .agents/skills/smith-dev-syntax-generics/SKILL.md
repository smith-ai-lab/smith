---
name: smith-dev-syntax-generics
description: "New syntax (#1428): generic parameters in brackets (`fn sum[T: num](xs: []T)`, `f[f32]()`), concepts as types (`a: num`, `typeof(a)`), type functions (`fn Pair(T: type) -> type`), `const`, compile-time `if`, reflection (`fields(T)`, `value.[f]`, `f.attr(...)`), variadics (`rest: ...`), `static_assert`. Load when writing generic or compile-time code in the new syntax."
---

# Generics, concepts and compile time

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Generic functions

```smith
fn push[T](xs: *mut List(T), x: T) { ... }        // T worked out from the arguments
fn sum[T: num](xs: []T) -> T { ... }              // T restricted to a concept
fn get[K, V](m: *Map(K, V), key: K) -> ?V { ... }
fn first[T, n: usize](data: [n]T) -> T { ... }    // a type and a compile-time value
fn alloc[T](n: usize) -> []T { ... }

xs.push(3)                                        // T = i32, from xs
ys := mem.alloc[i32](10)                          // no argument carries T: written at the call
sines := table[64]()
```

- Generic parameters go in brackets after the name: `T` (any type), `T: concept`, or `n: usize` (a compile-time value of that type). One instance per distinct set; there are no templates and no run-time dispatch.
- When an argument cannot supply a parameter, the call writes them all, in order. At the call, `f[f32]` parses as an index and the checker reads it as instantiation, so the parser stays context-free.

## Concepts

```smith
fn larger(a: num, b: typeof(a)) -> typeof(a) {    // a concept stands where a type goes
    if a > b { a } else { b }
}
fn larger2[T: num](a: T, b: T) -> T {             // the same, naming the type to share it
    if a > b { a } else { b }
}
```

- Concepts are lowercase: `int`, `float`, `num`, `signed`, `unsigned`, `pointer`, and so on. A concept can be used anywhere a type can, wherever the function can be monomorphized. A struct field and a function exported to C need concrete types.
- There are no traits and no user-defined dispatch: concepts answer "does this type support X?", and passing functions (`fn(...)` values, or a struct of them) answers "call the right code".

## Type functions

```smith
fn Pair(T: type) -> type {                        // a generic type is a function returning a type
    struct {                                      // the one unnamed struct; it takes the name Pair(T)
        first: T
        second: T
    }
}

IntPair := Pair(i32)
p := IntPair(first = 1, second = 2)
```

A `T: type` parameter is for a function that returns `type`; in any other function it is an error whose fix moves it into the brackets.

## Not settled yet

Pack expansion (today's `f(g(rest)...)`), `countof(rest)` and `rest[i]`, a computed result type (`-> leaf_of(T)`), the reflection members (`f.name`, `f.type`), which module a std generic's instance counts as for private fields, and whether `fields(T)` and a pack are iterable the way built-in types are.
