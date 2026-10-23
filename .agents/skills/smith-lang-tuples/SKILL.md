---
name: smith-lang-tuples
description: "Tuples in .smith: struct { i32, f32 }, a struct whose fields have no names; t[0], countof(t), return {a, b}, nesting, typedef, the same shape in two modules being one type, how a field type is spelled, the C struct and header a tuple becomes. Load when a .smith file writes `struct {` followed by types, indexes a value with t[0] or asks countof(t), or on 'the index of a tuple is a constant', 'is out of range', 'a tuple has at least one field', 'a tuple field is a type', 'a tuple type cannot be written in a generic function yet' or smith_tuple_."
---

# Tuples

This skill ships in the smith plugin, from `.agents/skills/smith-lang-tuples/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Overview: `smith-lang`; the long reference is [knowledge/smith-language.md](../../../knowledge/smith-language.md) ("Tuples").

A tuple is a struct whose fields have no names, written `struct { i32, f32 }` (a syntax error in C, so smith gives it a meaning). Its **identity is its shape**: the same field types in the same order are one type in every file and module.

```smith fragment
typedef struct { i32, f32 } Pair;              // a name for the type; Pair is still struct { i32, f32 }

struct { i32, f32 } make(i32 a, f32 b) {       // returned, passed by value or by pointer, stored in locals, fields, arrays
    return {a, b};                            // in a function that returns a tuple; (struct { i32, f32 }){a, b} anywhere
}

void use(void) {
    struct { i32, struct { f32, f32 } } n = {1, {2.0f, 3.0f}};   // tuples nest
    Pair p = make(3, 4.5f);
    p[0] += n[0];                             // t[0], t[1]: a constant index; written like a field
    usize count = countof(p);                 // 2
    struct { i32, f32 } *q = &p;
    (*q)[1] = n[1][0];                        // q->_1 also works
}
```

- **The index is a constant** (a number or a `constexpr` name). `t[i]` and `t[2]` for two fields are errors that name the tuple and its count. A tuple of no fields, `struct { }`, is an error (C has no empty struct).
- **A field is a type name** with stars and qualifiers (`const char *`, another tuple). An array, a function pointer or a `typeof` is named with a typedef first.
- **How the fields are spelled makes the type.** A field is named by what is written, with a module's names resolved: `Meters` in `geo::a` and `a::Meters` elsewhere are one spelling. The sized, stdint and basic C types are unified (`i32`, `int32_t`, `int` and `signed` are one; `i64` is `long long`; `usize` is `size_t`), but **a typedef is not looked through**: `struct { Meters, f32 }` and `struct { f64, f32 }` are different types though `Meters` is `f64` (assigning one to the other is clang's "incompatible types" error).
- **Underneath** `struct { i32, f32 }` is `struct smith_tuple_i32__f32 { int32_t _0; float _1; }`, defined where it is written (C23 accepts the same definition again); `t[0]` is `t._0`.
- **A generated header** holds the struct once, before the declaration that uses it, guarded by `#ifndef SMITH_TUPLE_i32__f32`, so C and C++ include it, even from C11 (`struct smith_tuple_i32__f32 p = geo_a_make(2, 3.5f); p._0`). A module whose exports use tuples has a `// smith-tuples` line, which tells an importer to index them.
- **Not yet:** a tuple type written inside a generic function (`struct { T, i32 }`) is an error that asks for a typedef outside; `t[0]` and `countof(t)` inside a generic's body are not rewritten (write `t._0`). Lists (`auto... rest`) becoming tuples is issue #95, ticket 6.

`smith fmt` leaves a tuple as written; `smith lint` and `smith lsp` do not judge the lowered struct or its `_0` fields by the naming rules.
