---
name: smith-lang-lambda-param
description: "`Lambda(F)`, the parameter type for code to run (`export void sort(auto xs[:], Lambda(Bool (typeof(xs[0]) *, typeof(xs[0]) *)) less)`): a lambda or a plain function as an argument, one instance each, F naming earlier parameters' types. Load when a parameter is `Lambda(...)`, when passing `[](Num *a, Num *b) -> Bool {...}` to such a function, or on 'does not have the type of `Lambda(', 'takes N parameters, but its type' or 'needs a type: write `auto'."
---

# `Lambda(F)`: code to run as a parameter

`Lambda(F)` (in scope with no import) is the parameter type for code to run; `F` is a C function type and may name earlier parameters (`typeof(xs[0])`). The function is a generic, one instance per lambda: zero cost, Enzyme sees through it, and a plain function may be passed too. Put it last, by library rule.

```smith
module sorting;

export void bubble(auto xs[:], Lambda(Bool (typeof(xs[0]) *, typeof(xs[0]) *)) less) {
    for (Usize i = 0; i + 1 < countof(xs); i++) {
        if (!less(&xs[i + 1], &xs[i])) continue;
        typeof(xs[0]) held = xs[i];
        xs[i] = xs[i + 1];
        xs[i + 1] = held;
    }
}

export void ascending(I32 xs[:]) {
    bubble(xs, [](Num *a, Num *b) -> Bool { return *a < *b; });   // a, b are xs's element type
}
```

- Parameters are written `auto a`, `auto *a` or a concept (`Num a`, preferred) and take F's types; a typed one (`I32 a`) is kept and checked. `[](a, b)` is an error. No `-> R`: R is F's.
- A wrong signature is an error naming F; a wrong parameter count too. A body captures nothing and holds no lambda or generic call; not yet: forwarding a `Lambda(F)` to another generic.

Reference: [knowledge/smith-lambdas.md](../../../knowledge/smith-lambdas.md). Lambdas themselves: `smith-lang-lambdas`.
