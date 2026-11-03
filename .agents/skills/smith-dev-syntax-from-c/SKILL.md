---
name: smith-dev-syntax-from-c
description: "Today's C-like .smith forms and what the new syntax (#1428) writes for each: `module`/`import`/`c::`, `export`, typedef, `T x;` declarations, `U32`/`Usize`, `T *`/`->`/`*p`, `(T)x`, `{.x = 1}`, `constexpr`, `[[...]]`, `switch`, three-part `for`, `xs[:]`. Load when porting a ticket, test or example to the new syntax, or writing the fix-it for a form."
---

# From today's forms to the new syntax

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428. In a module each left-hand form parses but is an error with a fix-it that writes the right-hand form; `smith lint --fix` applies every fix in a file. The switch-over runs it once over the whole tree (#1631).

| Today | New |
|---|---|
| `module game::replay;` | nothing: the module is the file's path (`game/replay.smith` is `game.replay`) |
| `import std::math;` then `math::sin(x)` | `m := import std.math` then `m.sin(x)` |
| `import <stdio.h>;` then `c::printf(...)` | `stdio := import c <stdio.h>` then `stdio.printf(...)` |
| `struct c::stat`, `c::EOF` | `st.stat`, `stdio.EOF` through the header's binding |
| `export U32 next() { ... }` | `pub fn next() -> u32 { ... }` |
| `export typedef struct { ... } Replay;` | `pub struct Replay { ... }` |
| `typedef Pair(I32) IntPair;` | `IntPair := Pair(i32)` |
| `U32`, `Usize`, `Bool`, `F64` | `u32`, `usize`, `bool`, `f64` |
| `int x = 3;`, `unsigned n;` | `x: i32 = 3`, `mut n: u32` (the fix names the sized type) |
| `const I32 a = 3;` | `a := 3` (constant is the default) |
| `I32 a = 3; a = 4;` | `mut a := 3` then `a = 4` |
| `constexpr I32 N = 64;` | `const N := 64` |
| `T *p`, `const T *p` | `p: *mut T`, `p: *T` |
| `p->x`, `*p` | `p.x`, `p.*` |
| `(F64)x` | `x as f64` |
| `Point p = {.x = 1, .y = 2};` | `p := Point(x = 1, y = 2)` |
| `Point p = {0};` | `mut p: Point` (zero) |
| `I32 xs[:]` | `xs: []i32` |
| `I32 xs[5]` | `xs: [5]i32` |
| `x ? a : b` | `if x { a } else { b }` |
| `switch (s) { case ...: }` | `match s { .circle(c) => ... }` |
| `for (Usize i = 0; i < n; i++)` | `for i in range(0, n)` |
| `for (Usize i, T x : xs)` | `for (i, x) in enumerate(xs)` |
| `if constexpr (c)` | `if c` with a compile-time `c` |
| `for constexpr (auto x : rest)` | `for x in rest` |
| `T f(constexpr Usize n)` | `fn f[n: usize]() -> T` |
| `auto f(auto x)` | `fn f[T](x: T)`, or a concept in the type's place: `fn f(x: num)` |
| `Lambda(F)` parameter | `fn(A) -> R` |
| `[[nodiscard]]` | nothing: an unused result is an error |
| `[[maybe_unused]] I32 x` | `_x` |
| `[[std::affine]]`, `[[gnu::always_inline]]` | `@affine`, `@inline` |
| `[[std::c_name("X")]]` | `@c_name("X")` |
| `#if defined(__APPLE__)` around declarations | `@when(target.os == .macos)` on each |
| `#define N 64` | `const N := 64` |
| `#define F(x) ...` | a function or a generic |
| `#pragma ...`, `#error "..."` | an attribute, `static_assert(...)` |
| `union` | a payload `enum` |
| `goto`, labels | `defer`, `break`, `continue`, `match` |
| `&mut x`, `impl`, `x?`, `.iter().map()` (other languages) | errors naming `&x`, a module's functions, `try x`, `for` |

Forms with no row are not settled: the "Not settled yet" section of each tour skill lists them.
