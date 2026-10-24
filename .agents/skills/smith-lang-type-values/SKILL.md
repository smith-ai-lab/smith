---
name: smith-lang-type-values
description: "Types as values in .smith: constexpr type t = i32;, a struct member constexpr type of;, an array of types, == and != across typedefs, sizeof(t), alignof(t), is_integer(t), `t x;` declaring a variable, an ordinary function that takes or returns type (bool small(type t)), a type value as the argument of a generic call. Load when a .smith file declares something of type `type`, or on 'is a `type`, which exists only at compile time', 'holds a type', 'has no value here' or 'a type as a value needs smith's compiler'."
---

# Types are values

This skill ships in the smith plugin, from `.agents/skills/smith-lang-type-values/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Overview: `smith-lang`; generics (`constexpr type T`): `smith-lang-generics`; the long reference is [knowledge/smith-generics.md](../../../knowledge/smith-generics.md) ("Types are values"). Needs a compile with smith's compiler (not `smith cc --smith-lower`).

`type` is a compile-time type. A `constexpr type` is a value; a struct, an array or a function can hold, take or return one.

```smith fragment
typedef struct { char name[16]; constexpr type of; } Field;   // `constexpr` on a member is invalid C: this is smith's
constexpr type count = u32;
constexpr type kinds[2] = {i32, f64};
constexpr Field length = {"length", usize};

static_assert(count == uint32_t);                  // == and != : equal exactly when the types are, typedefs looked through
static_assert(sizeof(kinds[1]) == 8 && alignof(length.of) == 8 && is_integer(count));
bool small(type t) { return sizeof(t) <= 8; }      // an ordinary function: it runs in the compiler
type wider(type a, type b) { return sizeof(a) >= sizeof(b) ? a : b; }
static_assert(small(i32) && wider(u8, f64) == double);

i32 main(void) {
    constexpr type local = wider(u8, i64);
    local big = 1;                                 // `t x;` declares a variable of the type t holds
    return (int)sizeof(big);                       // 8
}
```

- **Where a type name is a value:** after the `=` of a declaration of a `type` or of a struct that holds one, in the arguments of a call to a function that takes a type, in the `return` of a function that returns one, and beside `==` or `!=`. Elsewhere `i32` is the type, as in C (`sizeof(i32)`, `(i32)x`, `i32 x;`). `type` is not reserved: it is the keyword only where a declaration begins (`type t = i32;`, a parameter `type t`, a result type) and an ordinary identifier anywhere else (`int type;` as a member or a local, a parameter `c::Kind type`, `x.type`). Where a variable `type` is in scope, `type * n` is the product, as in C.
- **`t x;`, `(t)y`, `sizeof(t)`, `alignof(t)`, `is_integer(t)`, `is_float`, `is_signed`, `is_pointer`, `is_bool`, `is_enum`, `is_record`, `is_array`:** `t` is a name or a chain (`foo.of`, `kinds[1]`). A `constexpr type` read from an array or a member is known to the compiler only once the next statement has begun: use it from the statement after. A type value as the argument of a generic call stands for the type (`pool::width(t)`).
- **A type exists only at compile time.** `type x = i32;` or `Field f = {...};` without `constexpr` is an error naming the field that holds the type; a function that takes or returns `type` is for `static_assert`, `constexpr` initializers and a generic's `if constexpr (small(T))` (`T` is passed as a value). Use of one at run time in a function body is not checked beyond the declarations.
- **Exact for the target:** `sizeof(t)` of `long` is 4 with `--target=x86_64-pc-windows-msvc` and 8 on the host, because a type's handle is made from the compiler's own answer.
- **Not yet:** a type value in a generic function's body or as its parameter (`i32 f(constexpr type T, type t)`), a type returned from a function used where a type is written (`List(f32)`), `typeof(x)` as a type value, a type value in an exported function (a module's header), the editor's tree-sitter grammar for `kinds[1] x;`.
