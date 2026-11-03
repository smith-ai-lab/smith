---
name: smith-dev-syntax-privacy
description: "New syntax (#1428): zero values (`T{}`, `let mut xs = list.List(i32){}`), private fields and `pub` fields, the error `is private to`, building a struct from another module naming only `pub` fields, `fields(T)` and privacy. Load when a struct has private fields or an empty value is written in the new syntax."
---

# Zero values and private fields

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Zero values

```smith
let mut xs = list.List(i32){}       // the zero value, written out: an empty list
reset(list.List(i32){})
xs = list.List(i32){}
```

- `T{}` is the zero value of any struct. A declaration always has a value, so the zero value is written, never implied; `= undefined` opts out.
- `list.List(i32)` alone is the type, not a value; the checker says so.
- Every type's zero value is valid, so a type with private fields makes all-zero a valid value: an empty list has no storage and a count of 0, and `push` allocates.

## Private fields

```smith
pub struct List:
    pub len: usize                  // other modules read and write it
    data: *mut i32                  // only this module
```

- A field is private to its module unless marked `pub`; the defining module sees every field.
- In another module, naming a private field (`xs.data`, `List{data = p}`) is an error: `data is private to std.list; mark it pub or call a function of std.list`.
- Braces there may name only `pub` fields; every field not named, private ones included, is zero.
- `fields(T)` in another module lists only the `pub` fields.
- `pub` on a field of a struct that is not `pub` is an error.
- C sees every field in the generated header (the layout); C, C++ and Objective-C types have no privacy.
