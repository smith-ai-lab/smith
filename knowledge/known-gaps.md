---
type: design
title: Known gaps
description: A response file is opaque to .metal routing and Apple's metal output is not reproducible.
tags: [gaps]
---
# Known gaps

- A response file (`@file`) is opaque, so a `.metal` input inside one goes to clang.
- Apple's `metal` output is not reproducible (two identical builds of learner's shaders differ), so compare shaders by
  the command smith runs, not by bytes.
- `arena::push_slice(&a, T, sizes...)` (#82, #105, #107) gives a slice with a size for each `[:]` (`push_slice(&a, f32,
  n, m)` is `f32[:][:]`, `push_slice(&a, f32[2], n)` is `f32[:][2]`; its type is `arena::slice_of`, an exported function
  over `type`), and `arena::push_array` is gone (a module that goes through the raw lowering uses `push_array_raw`). The instance of a generic
  cannot name a local of its own body in a call that has an `auto` parameter (`push_slice(arena, char, characters +
  1)` where `characters` is that body's local: the parser has not seen it), so write the raw call or a cast of a
  parameter. The result of a
  generic call cannot be indexed or sliced where it is written (`push_slice(...)[1:3]`): bind it to a `T xs[:]` first. A
  module that imports arena is not in slice mode for that (the header carries no `smith-slices` line for a generic's
  head): it writes a `[:]` of its own to index.
- A slice in an aggregate initializer (`{.items = array}`) is C's brace elision (the pointer set, the count left 0), not a
  conversion; `-Wmissing-braces` reports it, so assign after declaring. `fields(T)`, `format` and `test::check_equal` over
  slices are not built (#82's comment of the lead; #83's `fields`).
- A type function reads a pointer's pointee one level deep (issue #143): `typeof(*(t)nullptr)` of `T *` is `T`, of `T **` is a `T *` that has no pointee of its own, so it cannot be walked twice (the error says so), and that `T *` is not `==` to a `T *` written out. A type handle is 128 bits and a pointer's upper half holds only its pointee's lower half; a general reader of types (a type-function interpreter in smith) waits for a use case. An `if constexpr` over a type function's type result (`small_of(T) == i64`, `sizeof(small_of(T))`) is not supported either: only a computed result type or a number or bool function works.
- A type function that returns an enum (`type option(type T) { return enum { T some; none; }; }`, issue #198) is an implementation limit, not a language rule: code written today keeps working when it is lifted. Its body is exactly `return enum {...};` and its parameters all `type` (anything else is an error naming this); its arguments are written type names, not a computed type (`option(typeof(f()))`); a use inside a function body is an error unless a declaration at file scope named the same `option(...)` first (`typedef option(usize) MaybeUsize;` does); it cannot be used inside a generic yet, nor in an exported declaration yet, and two `option(usize)` written in two modules are two structs of one name until the header carries it. `option(usize) == option(usize)` as type values is not built (the use is a struct, not a handle).
- A payload enum's dot rule in a generic's body (issue #198): `return .none;` and `return {.some = i};` work when the generic's declared result is a payload enum (a typedef'd name or `x::Name`), written per instance (`smith_lang_enum_replay`); a `.name` anywhere else in a generic's body (`s == .some`, `x = .none`, an argument, a `case`) is an error at the generic saying so, since the body is written at the end of the file without the compiler that knows the type. A result that is itself computed (`option(T)`) is not built yet.
