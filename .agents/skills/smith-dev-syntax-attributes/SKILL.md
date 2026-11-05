---
name: smith-dev-syntax-attributes
description: "New syntax (#1428): attributes are structs written with call syntax, `()` left off when empty (`@inline`, `@affine`, `@c_name(\"x\")`, `@when(target.os == .macos)`, `@effects(.alloc, .net)`, `@serial.rename(to = \"user_id\")`, `@serial.skip`), `@when`'s rules and its header `#if`, user attributes as `@attribute` structs, reading them by reflection, wrapping a function by an explicit call (`memo(compute)`), `_` for unused. Load when writing an attribute in the new syntax."
---

# Attributes

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Built-in attributes

```smith
@inline
fn dot(a: vec(f32, 4), b: vec(f32, 4)) -> f32: ...

@affine
pub struct Key:
    bits: u64

@c_name("smith_arena_push")
pub fn push(a: *mut Arena, size: usize) -> *mut u8: ...

@effects(.alloc, .net)
pub fn fetch(url: str) -> Result(str, net.Error): ...

@when(target.os == .macos)
fn open_window() -> Window: ...

@when(target.os != .macos)
fn open_window() -> Window: ...

@when(target.os == .macos)
import c <sys/event.h>
```

- Every attribute is a struct, written with call syntax on the line before what it marks. With no arguments the parentheses are left off (`@inline`, not `@inline()`; `smith fmt` drops them).
- Built-ins are lowercase with no namespace: `@inline`, `@noinline`, `@cold`, `@pure`, `@affine`, `@c_name("x")`, `@when(cond)`, `@effects(...)`. A new one (`@hot`, `@unroll`) exists once its own feature ticket decides it. `[[std::x]]` becomes `@x`.
- `@when(cond)`: a top-level declaration, struct field or import that exists only where the compile-time `cond` holds; it is the only top-level compile-time choice, there is no `if constexpr` (owner, 2026-11-04). A header imported under an untaken `@when` is never read. For each supported target (macOS and Linux), two live declarations of a name are an error, and so is a use with none. The generated header writes `#if`, around the `#include` for an import. Inside a function, a compile-time `if` chooses the code instead.
- `[[nodiscard]]` is gone (an unused result is an error); `[[maybe_unused]]` becomes a `_` prefix on the name.

## User attributes

```smith
@attribute
pub struct rename:
    to: str

struct User:
    @serial.rename(to = "user_id")
    id: u64
    @serial.skip
    cache: u64 = 0
```

A user attribute is a struct marked `@attribute`; using it builds the struct at compile time, positional or named, like any struct. It is qualified by its module. Attributes are data, not decorators: code reads one through reflection, `f.attr(serial.rename)` giving `?serial.rename`, and wrapping a function is an explicit call: `pub fast_compute := memo(compute)`.
