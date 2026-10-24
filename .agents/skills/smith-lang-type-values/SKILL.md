---
name: smith-lang-type-values
description: "Types as values in .smith: constexpr type t = i32;, a struct member constexpr type of;, an array of types, == and != across typedefs, sizeof(t), is_integer(t), `t x;` declaring a variable, an ordinary function that takes or returns type (bool small(type t)), `t[:]` as a slice type value. Load when a .smith file declares something of type `type`, or on 'is a `type`, which exists only at compile time', 'holds a type', 'has no value here' or 'a type as a value needs smith's compiler'."
---

# Types are values

`type` is a compile-time type: a `constexpr type` is a value that a struct, an array or a function can hold, take or return. C has no way to ask what a type is, so it uses macros; here a function over `type` is ordinary C run in the compiler.

```smith
module types;

typedef struct {
    char name[16];
    constexpr type of;                             // `constexpr` on a member is invalid C: this is smith's
} Column;

constexpr type count = u32;                        // a type is a value
constexpr type kinds[2] = {i32, f64};
constexpr Column length = {"length", usize};

static_assert(count == u32 && sizeof(count) == 4 && is_integer(count));   // == and != compare types
static_assert(alignof(length.of) == 8);

bool small(type t) {                               // an ordinary function: it runs in the compiler
    return sizeof(t) <= 8;
}

type wider(type a, type b) {
    return sizeof(a) >= sizeof(b) ? a : b;
}

constexpr bool fits = small(i32);                  // true: a call with a type name as its argument

export usize demo(void) {
    constexpr type local = wider(u8, i64);         // a function over types, at compile time
    constexpr type second = kinds[1];
    local big = 1;                                 // `t x;` declares a variable of the type t holds
    second ratio = 0.5;                            // a type read from an array: use it a statement later
    return sizeof(big) + (fits ? 1 : 0) + (usize)(ratio * 2);   // 8 + 1 + 1
}
```

## Rules

- **Where a type name is a value:** after the `=` of a declaration of a `type` or of a struct that holds one, in the arguments of a call to a function that takes a type, in the `return` of one that returns a type, and beside `==` or `!=`. Elsewhere `i32` is the type, as in C (`sizeof(i32)`, `(i32)x`). `type` is a keyword only where a declaration begins (`int type;` still works).
- **`t x;`, `(t)y`, `sizeof(t)`, `alignof(t)`, `integer(t)`, `floating`, `signed_integer`, `unsigned_integer`, `arithmetic`, `boolean`, `pointer`, `enumeration`, `structure`, `array`, `slice` (the C standard's nouns; `is_integer`, `is_float`, `is_signed`, `is_bool`, `is_enum`, `is_record`, `is_pointer`, `is_array`, `is_slice` still work, until a release has the nouns):** `t` is a name or a chain (`foo.of`, `kinds[1]`). A `constexpr type` read from an array or a member is known only from the next statement on. As the argument of a generic call it stands for the type (`pool::width(t)`).
- **A type exists only at compile time**: `type x = i32;` or `Column c = {...};` without `constexpr` is an error naming the field that holds the type, except in the body of a function that takes or returns `type` (it runs in the compiler only). Such functions serve `constexpr` initializers, a generic's result type and `if constexpr (small(T))`.
- `static_assert(small(i32))` and `static_assert(sizeof(kinds[1]) == 8)` build under `-Wpedantic -Werror`. Not yet: a type value as a generic's parameter (`i32 f(constexpr type T, type t)`), a type-returning call where a type is written (`List(f32)`).

## Errors

- ``` `x` is a `type`, which exists only at compile time ``` (without `constexpr`); ``` `c` holds a type ``` (the struct field); ``has no value here``; ``a type as a value needs smith's compiler`` (not `smith cc --smith-lower`).

`t[:]` as a slice type value, exported functions over `type`, the pointee of a pointer: `smith-lang-type-values-more`. Generics: `smith-lang-generics`.
