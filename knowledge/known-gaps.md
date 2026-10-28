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
- `arena::push_slice(&a, T, sizes...)` (#82, #105, #107) gives a slice with a size for each `[:]` (`push_slice(&a, F32,
  n, m)` is `F32[:][:]`, `push_slice(&a, F32[2], n)` is `F32[:][2]`; its type is `arena::SliceOf`, an exported function
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
- A type function reads a pointer's pointee one level deep (issue #143): `typeof(*(t)nullptr)` of `T *` is `T`, of `T **` is a `T *` that has no pointee of its own, so it cannot be walked twice (the error says so), and that `T *` is not `==` to a `T *` written out. A type handle is 128 bits and a pointer's upper half holds only its pointee's lower half; a general reader of types (a type-function interpreter in smith) waits for a use case. An `if constexpr` over a type function's type result (`small_of(T) == I64`, `sizeof(small_of(T))`) is not supported either: only a computed result type or a number or bool function works.
- A type function that returns an enum (`Type Maybe(Type T) { return enum { T some; none; }; }`, issue #198) is an implementation limit, not a language rule: code written today keeps working when it is lifted. Its body is exactly `return enum {...};` and its parameters all `type` (anything else is an error naming this); its arguments are written type names, not a computed type (`Option(typeof(f()))`); a use inside a function body defines its struct in the block in front of its statement (issue #425; C23 makes it the file-scope type), but `Option(Usize) x = .none;` as the very first mention is an error saying `.none` needs a type, since the compiler has not read the definition yet: write `{.some = v}`, or mention the type in an earlier statement; in a generic only a use with written arguments works (`Option(Usize) find(auto xs, auto v)`), not one with the generic's own parameters (`Option(T)`, `Option(typeof(xs[0]))`: not built yet). `Option(Usize) == Option(Usize)` as type values is not built (the use is a struct, not a handle). A module cannot export the type function itself yet, so each module writes its own `Type option(...)`.
- A value does not convert to `.ok` or `.some` (issue #358) inside a generic's body, nor in a struct's member initializer (`struct Box b = {.v = 5};`; `.none` there has no type either): write `{.ok = v}` and `{.some = v}`. A variable declared in a `for` header is not yet known to the compiler where the loop's body is one unbraced statement (`for (Usize i = 0; ...; i++) return i;`): the value is left as it is and C says it cannot return a `Usize` as an `option`; put the body in braces.
- A module's tag is a type name (issue #333) with limits: a payload enum cannot name itself by its bare name inside its own variants (the typedef is of the struct `smith_enum_<module>_<Name>`, written after it: write `struct <module>_<Name>`); a struct defined in a function's result type (`struct R {...} f() {...}`) has its typedef after the next `;`; a block's tag colliding with a variable of the same name is not reported (only file scope is); a tag declared in a generic's recorded body and a private tag an exported generic's body names are not tested; the explicit forward typedef `typedef struct Node Node;` before the definition still gives a `Node` that is not the module's `Node` (the lint of stage 3 replaces it).
- The range `for` (issue #243) has a block for a body (`for (T x : xs) stmt;` is an error naming the block, #377), takes a slice or a fixed array of one dimension (not a slice of rows, a pointer or a number), and over a fixed array or in a generic's body evaluates its range each turn, so there it is a name or a member and not a call (#378). `for constexpr (auto x : list)` takes `fields(T)` and a generic's `rest`; a constexpr array as the list is not built (#379), nor `parameters(F)` (#332).
- A payload enum's dot rule in a generic's body (issue #198): `return .none;` and `return {.some = i};` work when the generic's declared result is a payload enum (a typedef'd name or `x::Name`), written per instance (`smith_lang_enum_replay`); a `.name` anywhere else in a generic's body (`s == .some`, `x = .none`, an argument, a `case`) is an error at the generic saying so, since the body is written at the end of the file without the compiler that knows the type. A result that is itself computed (`Option(T)`) is not built yet.
