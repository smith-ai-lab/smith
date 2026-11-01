---
name: smith-lang-array-params
description: "Array parameters in a .smith module: `I32 xs[4]` keeps its size (checked indexes, countof(xs), `xs[static 4]` for clang and smith prove), `F32 m[3][2]`, why `I32 xs[]` and `sizeof(xs)` are errors, `char **argv` for main. Load when a module's parameter is written as an array, or on 'array parameter', 'an array parameter needs its size', 'pass an array of that shape', 'is not a pointer: it cannot be changed'."
---

# Array parameters: `I32 xs[4]`

In C a parameter `I32 xs[2]` is a pointer: the `2` is dropped. In a file with a `module` line it is kept. Only parameters change, and every change is an error, never a new meaning; a file with no `module` line means what it means in C.

```smith
module rows;

import std::contract;

export I32 pick(I32 xs[4], Usize i) {      // 4 objects: the header keeps the size
    contract::requires(i < 4);               // all `smith prove` needs: xs[i] is in the array
    return xs[i];
}

export F32 corner(F32 m[3][2]) {
    return m[countof(m) - 1][countof(m[0]) - 1];     // m[2][1]
}
```

## Rules

- `countof(xs)` is the size (`countof(m[0])` the next dimension), a `Usize` written from the parameter's own size.
- An argument to a module's function must be an array of the same dimensions and elements (`const` may be added): another shape, a pointer or `nullptr` is an error. A C function (`c::memcpy(xs, ...)`) takes the array as a pointer, as in C.
- Pointer behaviour is a pointer: `I32 *xs`. There is no cast from a pointer to an array parameter. A run-time length is a slice (`smith-lang-arrays`).
- Underneath it is the C pointer, same ABI. The compile writes `xs[static 4]`, so clang and `smith prove` know it is not null and has 4 objects. A function pointer's parameters and a plain `.smith` file's are not changed.

## Errors

- ``index 4 is out of range for the array parameter 'xs' of 4 elements``: a constant index out of range (`xs[3]` for `xs[2]`, `xs[-1]`, `m[0][2]`).
- ``an array parameter needs its size: write `T xs[N]`, `T xs[:]` for a length known at run time, or `T *xs` for a pointer``: `I32 xs[]`, `char *argv[]` included. `main` is `I32 main(I32 argc, char **argv)`.
- ``sizeof of the array parameter 'xs' is the size of a pointer here: write countof(xs) ...``
- ``the array parameter 'xs' is not a pointer: it cannot be changed``: `xs++`, `xs += 1`, `xs = p`.
- `countof(xs)` alone does not count as a use of the parameter: the body must read it too (issue #178).
