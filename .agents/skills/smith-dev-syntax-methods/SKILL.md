---
name: smith-dev-syntax-methods
description: "New syntax (#1428): method calls (UFCS) `xs.push(5)` for `list.push(&xs, 5)`: looked up in the type's module and the current module, the one way where it applies, shadowing and field clashes, generics. Load when writing or checking a method call in the new syntax."
---

# Method calls (UFCS)

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Method calls (UFCS)

```smith
list := import std.list
m := import std.math
sum := m.sum                     // an alias is a declaration in this module

fn total(xs: *list.List(i32)) -> i32 { ... }

pub fn main() {
    mut xs: list.List(i32)
    xs.push(5)                   // list.push(&xs, 5): push is in List's module
    t := xs.total()              // total is in this module
    s := [1, 2, 3].sum()         // built-in types: this module's functions only
}
```

- `x.f(args)` is `f(x, args)`, or `f(&x, args)` when `f`'s first parameter is a pointer (`*mut` needs `x` to be `mut`).
- `f` is looked up in exactly two places: the module that defines `x`'s type, and the current module. No other module.
- Where the method form applies it is the one way: `smith fmt` rewrites `list.push(&xs, 5)` to `xs.push(5)`.
- A current-module function with the name of one in the type's module taking the same type is a shadowing error; a field and a function with one name on one type is an error.
- In a generic function, `x.f()` on a `T` resolves per instance at compile time, in the generic's module and in `T`'s module.
