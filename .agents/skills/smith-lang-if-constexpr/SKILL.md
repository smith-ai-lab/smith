---
name: smith-lang-if-constexpr
description: "`if constexpr` in .smith: in a generic's body it chooses the branch for each instance by the type (typeof(value) == f32, is_integer, sizeof, is_slice, static_assert(false) in a final else); in any other function it chooses by the target. Load on `if constexpr`, `is_integer(`, `is_float(`, a branch that must not compile for some type, or 'in `if constexpr`: ... is not a constant'."
---

# `if constexpr`

C has `#if` for code that may not compile everywhere and nothing for code that may not compile for every type. `if constexpr` is both: the branch not taken is never given to the parser.

```smith
module kinds;

export i32 describe(auto value) {
    if constexpr (typeof(value) == f32) {
        return (i32)value;                                 // here value is a float
    } else if constexpr (is_integer(typeof(value)) && sizeof(typeof(value)) == 8) {
        return 2 * (i32)value;
    } else {
        static_assert(false, "describe: not a number");   // a constraint: a static_assert in a final else
    }
}

export i32 demo() {
    return describe(1.5f) + describe((i64)3);              // 1 + 6
}
```

## In a generic

The condition is a constant expression: integers, `true`, `false`, C's operators, `sizeof(T)`, `alignof(T)`, `T == U` and `T != U` (`T` may be `typeof(x)`), and `is_integer(T)`, `is_float`, `is_signed`, `is_pointer`, `is_bool`, `is_enum`, `is_record`, `is_array`, `is_slice` (`T[:]`, a slice: `smith-lang-arrays`). The branch not taken may use what `T` has not (`value.x`). It also reads a C type (`T == c::V2`) and `target::` facts.

A call or a field in the condition (`table_size(sizeof(T)) > 64`, a `for constexpr` bound `i < config.count`) is evaluated by the compiler, so it must give an integer constant. The function must be defined in the file the instance is written in, an exported function of an import, or a private one of the generic's own module (`smith-lang-constexpr`, `smith-lang-type-values`): the importer defines what it mentions.

## Anywhere else

In a function that is not a generic the condition is `target::` facts, `constexpr` constants and literals (`if constexpr (target::arch == target::aarch64) { } else { }`, `smith-lang-target`), with blocks for branches.

## Around declarations

In a module, `if constexpr` also stands at file scope, around imports, functions, constants and exports: the branch taken is at file scope (its braces open no scope) and the branch not taken is never read, so its C headers are not loaded. Imports still come first: a branch of only imports may sit among the import lines.

```smith
module clock;

import target;

if constexpr (target::os == target::macos) {
    import <libproc.h>; // a macOS header: on Linux this branch is never read
}

if constexpr (target::os == target::linux) {
    export i32 ticks_per_second() {
        return 100;
    }
} else {
    export i32 ticks_per_second() {
        return 1000;
    }
}
```
The module's header has the same choice as `#if`, so a C includer gets the branch of its own target. `smith fmt` splits a C header with a `/` in its name inside a branch (`import <mach/mach_time.h>;`): put such imports in a platform module.

## Errors

- ``in `if constexpr`: `n` is not a constant``
- a `static_assert(false, "...")` in the final `else` fires at the call that asked: ``static assertion failed: describe: not a number`` and the note ``in instance kinds::describe(char *), requested from here``.

Loops over arguments: `smith-lang-rest`. Generics: `smith-lang-generics`.
