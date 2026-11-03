---
name: smith-lang-methods
description: "Method calls in the new syntax (#1428): `xs.push(5)` for `list.push(&xs, 5)`, looked up in the module that defines the type of `xs` and in the current module; `smith lint --fix` rewrites the module-qualified call. Load when writing `x.f(args)`, on 'has no method', 'does not take a ... first', 'would be ambiguous', or when `smith lint --fix` changed `list.push(&xs, 5)`."
---

# Method calls

`x.f(args)` is the call `f(x, args)`, or `f(&x, args)` when `f` takes a pointer first. There is one way to write it: where the method form applies, `smith lint --fix` rewrites the module-qualified call. The tour is [smith-dev-syntax-methods](../smith-dev-syntax-methods/SKILL.md).

```smith
list := import lib.list

fn run() -> i32 {
    mut xs := list.make()
    xs.push(5)                  // list.push(&xs, 5): push is in List's module; `mut` because push takes *mut
    xs.size()                   // list.size(&xs)
}
```

## Where `f` is looked up
- In the module that defines the type of `x` (seen through one pointer), and in the current module. No other module is searched.
- A built-in type has no module, so only the current module applies.
- A field and a function with one name on one type is an error, and so is a current-module function that takes the same type as the type module's function of its name (`would be ambiguous`: rename one).

## What `smith lint --fix` rewrites
`m.f(a, b)` becomes `a.f(b)` when `m` is an imported module, the type of `a` is a record `m` defines, `f` is a `pub` function of `m` whose first parameter is that type, and `a` is what the method form passes: `&x` for a function that takes a pointer, a pointer passed on, a value for a function that takes the value.

| written | fixed to |
| --- | --- |
| `list.push(&xs, 5)` | `xs.push(5)` |
| `list.size(p)` with `p: *List` | `p.size()` |
| `list.first(v)` with `v: List` | `v.first()` |

A call it does not apply to stays as written: a function of another module than the type's, a temporary where the function takes a pointer (`list.size(&list.make())`), a value passed where a pointer is taken. The fix reads the imported modules from the project's source folder and smith's library folder, so a module it cannot find leaves its calls alone.
