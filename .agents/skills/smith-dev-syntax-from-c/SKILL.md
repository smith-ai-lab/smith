---
name: smith-dev-syntax-from-c
description: "Today's C-like .smith forms and what the new syntax (#1428) writes for each: `module`/`import`/`c::`, `export`, typedef, `T x;` declarations, `U32`/`Usize`, `T *`/`->`/`*p`, `(T)x`, `{.x = 1}`, `constexpr`, `[[...]]`, `switch`, three-part `for`, `xs[:]`, `{ }` blocks. Load when porting a ticket, test or example to the new syntax, or writing the fix-it for a form."
---

# From today's forms to the new syntax

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428. In a module each left-hand form parses but is an error with a fix-it that writes the right-hand form; `smith lint --fix` applies every fix in a file. The switch-over runs it once over the whole tree (#1631).

| Today | New |
|---|---|
| `module game::replay;` | nothing: the module is the file's path (`game/replay.smith` is `game.replay`) |
| `import std::math;` then `math::sin(x)` | `import std.math` then `math.sin(x)` |
| `import <stdio.h>;` then `c::printf(...)` | `import c <stdio.h>` then `stdio.printf(...)` |
| `struct c::stat`, `c::EOF` | `stat.stat`, `stdio.EOF` through the header's name |
| `export U32 next() { ... }` | `pub fn next() -> u32:` and an indented body |
| `export typedef struct { ... } Replay;` | `pub struct Replay:` and indented fields |
| `typedef Pair(I32) IntPair;` | `let IntPair = Pair(i32)` |
| `U32`, `Usize`, `Bool`, `F64` | `u32`, `usize`, `bool`, `f64` |
| `int x = 3;`, `unsigned n;` | `let x: i32 = 3`, `let mut n: u32 = 0` (the fix names the sized type) |
| `const I32 a = 3;` | `let a = 3` (constant is the default) |
| `I32 a = 3; a = 4;` | `let mut a = 3` then `a = 4` |
| `I32 buf[64];` filled later | `let mut buf: [64]i32 = undefined` |
| `constexpr I32 N = 64;`, `#define N 64` | `const N = 64` |
| `T *p`, `const T *p`, `p->x`, `*p` | `p: *mut T`, `p: *T`, `p.x`, `p.*` |
| `NULL` | `.none` of a `?*T` |
| `(F64)x`, `(U8 *)p` | `x.to(f64)` (or `x.wrap(u8)` to keep the low bits), `p.cast(*u8)` |
| `Point p = {.x = 1, .y = 2};` | `let p = Point{x = 1, y = 2}` |
| `Point p = {0};` | `let mut p = Point{}` |
| `I32 xs[:]`, `I32 xs[5]`, `xs[:]` | `xs: []i32`, `xs: [5]i32`, `xs` |
| `{ ... }` | `:` and an indented block; `do:` where a block is a value |
| `x ? a : b` | `if x: a else: b` |
| `switch (s) { case ...: }` | `match s:` and arms `.circle(c): ...` |
| `for (Usize i = 0; i < n; i++)`, `i++` | `for i in range(0, n):`, `i += 1` |
| `for (Usize i, T x : xs)` | `for (i, x) in enumerate(xs):` |
| `if constexpr (c)`, `for constexpr (auto x : rest)` | `if c:` with a compile-time `c`, `for x in rest:` |
| `T f(constexpr Usize n)` | `fn f[n: usize]() -> T:` |
| `auto f(auto x)` | `fn f[T](x: T):`, or a concept in the type's place: `fn f(x: num):` |
| `Lambda(F)` parameter | `fn(A) -> R` |
| `[[nodiscard]]`, `[[maybe_unused]] I32 x` | nothing (an unused result is an error), `_x` |
| `[[std::affine]]`, `[[gnu::always_inline]]`, `[[std::c_name("X")]]` | `@affine`, `@inline`, `@c_name("X")` |
| `[[std::effects(alloc, net)]]` | `@effects(alloc, net)` |
| `#if defined(__APPLE__)` around declarations | `@when(target.os == .macos)` on each |
| `#define F(x) ...`, `#pragma`, `#error "..."` | a function or a generic, an attribute, `static_assert(...)` |
| `extern I32 errno;`, `extern int f(void);` | `extern let mut errno: i32`, `extern fn f() -> i32` |
| `static I32 count;` at file scope | `let mut count: i32 = 0` |
| `union`, `goto` and labels | a payload `enum`; `defer`, `break`, `continue`, `match` |
| `&mut x`, `impl`, `x?`, `.iter().map()` (other languages) | errors naming `&x`, a module's functions, `try x`, `for` |

Forms with no row are not settled: the "Not settled yet" section of each tour skill lists them. Asm, `volatile`, atomics, bitfields, `alignas` and `[[gnu::format(...)]]` wait until after the switch-over.
