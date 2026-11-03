---
name: smith-dev-syntax-compile-time
description: "New syntax (#1428): `const`, compile-time `if`, unrolled `for f in fields(T)`, reflection (`value.[f]`, `f.attr(...)`), variadics `rest: ...`, `static_assert`, and what replaced `constexpr`. Load when writing compile-time or reflective code in the new syntax."
---

# Compile time, reflection and variadics

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Compile time

```smith
const N := 64                                     // const demands compile-time evaluation

fn table[n: usize]() -> [n]f32 {
    for i in range(0, n) { m.sin(i as f32) }
}

fn show[T](value: *T) {
    for f in fields(T) {                          // a compile-time list: the loop is unrolled
        if num(typeof(value.[f])) {               // a compile-time condition keeps one branch
            stdio.printf("%g\n", value.[f] as f64)
        }
    }
}

static_assert(N % 8 == 0)
```

- `constexpr`, `if constexpr`, `for constexpr` and `const` parameters are removed: a compile-time value a function is generic over is a generic parameter.
- `fields(T)` in another module lists only `pub` fields. `value.[f]` reads the field `f` names. `f.attr(serial.rename)` gives `?serial.rename` ([smith-dev-syntax-modules](../smith-dev-syntax-modules/SKILL.md)).

## Variadics

```smith
fn each(rest: ...) {                              // each argument keeps its own type
    for (i, x) in enumerate(rest) { show(&x) }
}
```
