---
name: smith-dev-syntax-attributes
description: "New syntax (#1428): built-in attributes (`@inline`, `@cold`, `@pure`, `@affine`, `@c_name`, `@when(target.os == .macos)`), `@when`'s rules and its header `#if`, user attributes as `@attribute` structs with named fields, reading them by reflection, `_` for unused. Load when writing an attribute in the new syntax."
---

# Attributes

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Attributes

```smith
@inline
fn dot(a: vec(f32, 4), b: vec(f32, 4)) -> f32 { ... }

@affine
pub struct Key {
    bits: u64
}

@c_name("smith_arena_push")
pub fn push(a: *mut Arena, size: usize) -> *mut u8 { ... }

@when(target.os == .macos)
fn open_window() -> Window { ... }

@when(target.os != .macos)
fn open_window() -> Window { ... }
```

- Built in, so no namespace: `@inline`, `@noinline`, `@cold`, `@pure`, `@affine`, `@c_name("x")`, `@when(cond)`, and the effects attribute.
- `@when(cond)`: a top-level declaration or struct field that exists only where the compile-time `cond` holds. For each supported target (macOS and Linux), two live declarations of a name are an error, and so is a use with none. The generated header writes `#if`. Inside a function, a compile-time `if` chooses the code instead.
- Written on the line before what it marks, typically; `smith fmt` puts a user attribute on its own line.
- `[[nodiscard]]` is gone (an unused result is an error); `[[maybe_unused]]` becomes a `_` prefix on the name.

## User attributes

```smith
@attribute
pub struct rename {
    to: str
}

struct User {
    @serial.rename(to = "user_id")
    id: u64
}
```

A user attribute is a struct marked `@attribute`; using it builds the struct at compile time, with every field named. It is qualified by its module. It has no behaviour of its own: code reads it through reflection, `f.attr(serial.rename)` giving `?serial.rename`.

## Not settled yet

The effects attribute's spelling (today's `[[std::effects(alloc, net)]]`); C attributes such as `[[gnu::format(printf, 1, 2)]]`.
