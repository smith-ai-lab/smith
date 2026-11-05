---
name: smith-dev-syntax-methods
description: "New syntax (#1428): method calls (UFCS) `xs.push(5)` for `list.push(&xs, 5)`: looked up in the type's module, the current module (names brought in with `from ... import` count) and the built-in functions (`x.to(i64)`), the one way where it applies, shadowing and field clashes, generics. Load when writing or checking a method call in the new syntax."
---

# Method calls (UFCS)

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

```smith
import std.list
from std.math import sum

fn total(xs: *list.List(i32)) -> i32: ...

pub fn main() -> i32:
    mut xs := list.init(i32)        # init is a plain function of std.list
    xs.push(5)                      # list.push(&xs, 5): push is in List's module
    t := xs.total()                 # total is in this module
    s := [1, 2, 3].sum()            # sum was imported into this module by name
    n := t.to(i64)                  # a built-in function
    0
```

- `x.f(args)` is `f(x, args)`, or `f(&x, args)` when `f`'s first parameter is a pointer (`*mut` needs `x` to be `mut`).
- `f` is looked up in the module that defines `x`'s type, in the current module (including names brought in with `from ... import`) and among the built-in functions (`to`, `wrap`, `cast`, `countof`, `sizeof`, `alignof`). No other module.
- Where the method form applies it is the one way: `smith fmt` rewrites `list.push(&xs, 5)` to `xs.push(5)`. A call whose first argument is a type, `list.init(i32)`, stays a plain call.
- A current-module function with the name of one in the type's module taking the same type is a shadowing error; a field and a function with one name on one type is an error.
- In a generic function, `x.f()` on a `T` resolves per instance at compile time, in the generic's module and in `T`'s module.
- `methods(T)` lists the functions in `T`'s module whose first parameter is `T`, `*T` or `*mut T` ([smith-dev-syntax-compile-time](../smith-dev-syntax-compile-time/SKILL.md)).
