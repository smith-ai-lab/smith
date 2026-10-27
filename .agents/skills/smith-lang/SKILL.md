---
name: smith-lang
description: "The .smith language: C with modules, a superset of C by opt-in steps, and which skill teaches each step. Load when a file ends in .smith, on `module`, `import` or `export` in C source, smith cc --smith-header or --smith-lower, or 'needs a smith whose clang has the token hook'."
---

# `.smith` files

A `.smith` file is C. Renaming `x.c` to `x.smith` changes nothing; the file opts into more, one step at a time, and no step needs the one before.

```smith
module stats;                                  // a module: exports, private names, no header to write

import smith::contract;                        // an import: smith's contract module

export typeof(x) clamp(auto x, typeof(x) low, typeof(x) high) {   // generic: the type is the argument's
    contract::requires(low <= high);           // a precondition that runs and that `smith prove` proves
    return x < low ? low : x > high ? high : x;
}

export u32 total(const u32 xs[:]) {            // a slice: a pointer and its count
    u32 sum = 0;
    for (usize i = 0; i < countof(xs); i++) sum += xs[i];   // xs[i] is bounds-checked
    return sum;
}
```

## The steps

| Step | Write | Skill |
| --- | --- | --- |
| sized types | `u32`, `f64`, `usize` | `smith-lang-sized-types` |
| imports | `import lib::text;`, `import <math.h>;` then `c::sqrtf` | `smith-lang-modules`, `smith-lang-c-interop` |
| modules | `module a::b;`, `export` | `smith-lang-modules`, `smith-lang-exports` |
| slices, the range `for` | `i32 xs[:]`, `xs[a:b]`, `countof(xs)`, `for (i32 x : xs)` | `smith-lang-arrays`, `smith-lang-for` |
| generics | `constexpr type T`, `auto x`, `typeof(x)`, `if constexpr` | `smith-lang-generics` |
| argument lists, reflection | `auto... rest`, `fields(T)` | `smith-lang-rest`, `smith-lang-reflection` |
| concepts | `arithmetic x`, `integer n = 3;` | `smith-lang-concepts` |
| compile time, type values | `constexpr i32 x = fib(10);`, `constexpr type t = i32;` | `smith-lang-constexpr`, `smith-lang-type-values` |
| tuples, used once | `struct { i32, f32 }`, `[[smith::affine]]` | `smith-lang-tuples`, `smith-lang-affine` |
| options as named arguments | `open(.width = 640)`, zero is the default | `smith-lang-named-arguments` |
| enums with payloads | `typedef enum { Circle circle; empty; } Shape;`, `s == .circle` | `smith-lang-enums` |
| cleanup on every exit, a failure passed up, what a function may touch, unused on purpose | `defer c::close(fd);`, `try f()`, `[[smith::pure]]`, `_ = f();` | `smith-lang-defer`, `smith-lang-try`, `smith-lang-effects`, `smith-lang-unused` |
| the target, the io | `import target;`, `i32 main(io::Io *io)` | `smith-lang-target`, `smith-io` |

Formatting, linting, building, the editor: `smith-lang-tools`. Good style: `smith-idiom`.

## Compiling

C2y: `smith cc x.smith` means `-x c -std=gnu2y` (a `-std=` of yours wins), with the other flags as for any C file.

```sh
smith cc main.smith stats.smith -o prog   # main.smith: `module main; import stats;`
smith cc -c main.smith                    # alone: stats.smith is found by the path its name implies
```
`import stats` with no `stats.h` reads `stats.smith` (among the command's files, else by its path: `a::b` is `a/b.smith`); nothing is written, and linking is as in C. `smith build` writes the headers.

## Errors

- `.smith files need a smith whose clang has the token hook`: a stage0, or a smith built without `patches/clang-token-hook.patch`; `smith cc --smith-which x.smith` says `token-hook yes` or `no`.
- ``` `double` is a C keyword; it cannot name a function ```: a C keyword as a name (`i32 double(i32 x)`), at the name, and nothing follows it.
- A module not found is one error listing what was tried. `... is a C name; write c::...` and the module errors are in `smith-lang-modules-errors`. Every error points at the `.smith` text; if a file fails like plain C, it is C: fix it as C.
