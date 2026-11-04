---
name: smith-dev-syntax-privacy
description: "New syntax (#1428): private fields and `pub` fields, the error `is private to`, `init` functions as constructors (`pub fn init(T: type) -> List(T)`, `list.init(i32)`), field defaults instead of zero values (`retry: bool = false`), building a struct from another module, `fields(T)` and privacy. Load when a struct has private fields or defaults, or when an empty value is written in the new syntax."
---

# Private fields, init functions and defaults

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Private fields

```smith
pub fn List(T: type) -> type:       # in std.list
    struct:
        pub len: usize = 0          # other modules read and write it
        data: ?*mut T = .none       # only this module
```

- A field is private to its module unless marked `pub`; the defining module sees every field.
- In another module, naming a private field (`xs.data`, `List(data = p)`) is an error: `data is private to std.list; mark it pub or call a function of std.list`.
- A struct with a private field is built outside its module only by a function of that module.
- `fields(T)` in another module lists only the `pub` fields.
- `pub` on a field of a struct that is not `pub` is an error.
- C sees every field in the generated header (the layout); C, C++ and Objective-C types have no privacy.

## init functions

```smith
pub fn init(T: type) -> List(T):    # a plain function: no special rule
    List(T)()                       # in its own module, every field has a default

let mut xs = list.init(i32)         # an empty list
reset(list.init(i32))
```

- A constructor is an ordinary function named `init` in the type's module, taking a type or any other arguments it needs. Nothing about the name is special.
- `list.List(i32)` alone is the type, not a value; the checker says so.

## Defaults instead of zero values

```smith
pub struct Options:
    pub retry: bool = false
    pub port: u16 = 443

let a = Options()                   # every field has a default
let b = Options(port = 80)
```

- There are no zero values: every field is given, unless the struct declares a default. A default is a compile-time constant. `T()` is not an empty value; it builds a struct only when every field has a default.
- A declaration always has a value; `= undefined` opts out.
