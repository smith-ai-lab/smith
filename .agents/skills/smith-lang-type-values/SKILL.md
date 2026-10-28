---
name: smith-lang-type-values
description: "Types as values in .smith: constexpr type t = I32;, a struct member constexpr type of;, an array of types, == and != across typedefs, sizeof(t), is_integer(t), `t x;` declaring a variable, an ordinary function that takes or returns type (bool small(type t)), `t[:]` as a slice type value. Load when a .smith file declares something of type `type`, or on 'is a `type`, which exists only at compile time', 'holds a type', 'has no value here' or 'a type as a value needs smith's compiler'."
---

# Types are values

`type` is a compile-time type: a `constexpr type` is a value that a struct, an array or a function can hold, take or return. C has no way to ask what a type is, so it uses macros; here a function over `type` is ordinary C run in the compiler.

```smith
module types;

typedef struct {
    char name[16];
    constexpr Type of;                             // `constexpr` on a member is invalid C: this is smith's
} Column;

constexpr Type count = U32;                        // a type is a value
constexpr Type Block = U8[16];                     // a full C type, as in a cast
constexpr Type kinds[2] = {I32, F64};
constexpr Column length = {"length", Usize};

static_assert(count == U32 && sizeof(count) == 4 && Int(count));   // == and != compare types
static_assert(alignof(length.of) == 8 && sizeof(Block) == 16);

Bool small(Type t) {                               // an ordinary function: it runs in the compiler
    return sizeof(t) <= 8;
}

Type wider(Type a, Type b) {
    return sizeof(a) >= sizeof(b) ? a : b;
}

constexpr Bool fits = small(I32);                  // true: a call with a type name as its argument

export Usize demo() {
    constexpr Type local = wider(U8, I64);         // a function over types, at compile time
    constexpr Type second = kinds[1];
    local big = 1;                                 // `t x;` declares a variable of the type t holds
    second ratio = 0.5;                            // a type read from an array: use it a statement later
    return sizeof(big) + (fits ? 1 : 0) + (Usize)(ratio * 2);   // 8 + 1 + 1
}
```

## Rules

- **Where a type name is a value:** after the `=` of a declaration of a `type` or of a struct that holds one, in the arguments of a call to a function that takes a type, in the `return` of one that returns a type, and beside `==` or `!=`. Elsewhere `I32` is the type, as in C (`sizeof(I32)`, `(I32)x`). `type` is a keyword only where a declaration begins (`int type;` still works).
- **`t x;`, `(t)y`, `sizeof(t)`, `alignof(t)`, `Int(t)`, `Float(t)`, `Signed(t)`, `Unsigned(t)`, `Num(t)`, `Pointer(t)`, `Enum(t)`, `Struct(t)`, `Array(t)`, `Slice(t)` (`Bool` has no question: write `t == Bool`):** `t` is a name or a chain (`foo.of`, `kinds[1]`). A `constexpr type` read from an array or a member is known only from the next statement on. As the argument of a generic call it stands for the type (`pool::width(t)`).
- **A type value may be any C type**, written as C writes it in a cast: `Response (*)(Request *)`, `U8[16]`, `F32[2][3]`, `I32 (*)[4]`, `Request *[4]`. `t x;` declares it (`Handler h = f;`, `Block b = {};`, lowered to `typeof(...) x`). A name of another alias inside the type is not read: write the type.
- **`export constexpr Type Meters = F64;`** is `typedef double geo_Meters;` in the module's header, so C callers and importers write it as a type (`geo::Meters x`); `[[std::c_name("X")]]` names the typedef. A function pointer is `typedef void (*geo_Handler)(int32_t);`, an array `typedef uint8_t geo_Block[16];`. An export computed by a call (`= wider(U8, I64)`) has no C spelling: it stays out of the header. Not yet: `constexpr Type t = geo::Meters;` (a qualified name as a type value).
- **A type exists only at compile time**: `Type x = I32;` or `Column c = {...};` without `constexpr` is an error naming the field that holds the type, except in the body of a function that takes or returns `type` (it runs in the compiler only). Such functions serve `constexpr` initializers, a generic's result type and `if constexpr (small(T))`.
- `static_assert(small(I32))` and `static_assert(sizeof(kinds[1]) == 8)` build under `-Wpedantic -Werror`. Not yet: a type value as a generic's parameter (`I32 f(constexpr Type T, Type t)`), a type-returning call where a type is written (`List(F32)`).

## Errors

- ``` `x` is a `type`, which exists only at compile time ``` (without `constexpr`); ``` `c` holds a type ``` (the struct field); ``has no value here``; ``a type as a value needs smith's compiler`` (not `smith cc --smith-lower`).

`t[:]` as a slice type value, exported functions over `type`, the pointee of a pointer: `smith-lang-type-values-more`. Generics: `smith-lang-generics`.
