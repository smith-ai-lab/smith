---
name: smith-lang-tuples
description: "Tuples in .smith: struct { i32, f32 }, a struct whose fields have no names; t[0], countof(t), return {a, b}, nesting, typedef, the same shape in two modules being one type, how a field type is spelled, the C struct and header a tuple becomes. Load when a .smith file writes `struct {` followed by types, indexes a value with t[0] or asks countof(t), or on 'the index of a tuple is a constant', 'is out of range', 'a tuple has at least one field', 'a tuple field is a type', 'a tuple type cannot be written in a generic function yet' or smith_tuple_."
---

# Tuples

C returns two values through a pointer or a named struct. A tuple is a struct whose fields have no names, written `struct { I32, F32 }` (a syntax error in C, so smith gives it a meaning). Its **identity is its shape**: the same field types in the same order are one type in every file and module.

```smith
module pairs;

export typedef struct { I32, F32 } Pair;           // a typedef names it; Pair is still struct { i32, f32 }

export struct { I32, F32 } make(I32 a, F32 b) {    // returned, passed, stored in locals, fields, arrays
    return {a, b};                                 // in a function that returns a tuple
}

export F32 demo() {
    struct { I32, struct { F32, F32 } } n = {1, {2.0f, 3.0f}};   // tuples nest
    Pair p = make(3, 4.5f);
    p[0] += n[0];                                  // p[0], p[1]: a constant index, written like a field
    Usize count = countof(p);                      // 2
    struct { I32, F32 } *q = &p;
    (*q)[1] = n[1][0];                             // q->_1 also works
    return (F32)p[0] + p[1] + (F32)count;          // 4 + 2 + 2
}
```

`(struct { I32, F32 }){a, b}` builds one anywhere; `smith fmt` leaves a tuple as written.

## Rules

- **The index is a constant** (a number or a `constexpr` name). A tuple of no fields, `struct { }`, is an error (C has no empty struct).
- **A field is a type name** with stars and qualifiers (`const char *`, another tuple). An array, a function pointer or a `typeof` is named with a typedef first.
- **How the fields are spelled makes the type.** A module's names are resolved (`Meters` in `geo::a` and `a::Meters` elsewhere are one spelling); sized, stdint and basic C types are unified (`I32`, `int32_t`, `int`, `signed` are one; `I64` is `long long`; `Usize` is `size_t`). **A typedef is not looked through**: `struct { Meters, F32 }` and `struct { F64, F32 }` are different types though `Meters` is `F64`.
- **Underneath** `struct { I32, F32 }` is `struct smith_tuple_i32__f32 { int32_t _0; float _1; }` and `t[0]` is `t._0`. A generated header holds the struct once, guarded by `#ifndef SMITH_TUPLE_i32__f32`, so C and C++ include it, even from C11 (`p._0`); a `// smith-tuples` line tells an importer to index them.
- **Not yet, inside a generic**: a tuple type written there (`struct { T, I32 }`: use a typedef outside); `t[0]` and `countof(t)` are not rewritten in its body (write `t._0`). `auto... rest` is the arguments' tuple with its own spelling (`smith-lang-rest`).

## Errors

- ``the index of a tuple is a constant: `struct { I32, F32 }` has 2 fields, 0 to 1``
- ``the index 2 is out of range: `struct { I32, F32 }` has 2 fields``
- `a tuple has at least one field`; `a tuple field is a type` (name an array or function pointer with a typedef).
- assigning `struct { Meters, F32 }` to `struct { F64, F32 }` is clang's `incompatible type` error.

Overview: `smith-lang`. Reference: [knowledge/smith-language.md](../../../knowledge/smith-language.md) ("Tuples").
