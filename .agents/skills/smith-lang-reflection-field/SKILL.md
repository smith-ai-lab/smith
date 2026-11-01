---
name: smith-lang-reflection-field
description: "`fields(T)[i]` as a `Field` value in .smith: `constexpr Field f = fields(T)[i];`, `f.name`, `f.type`, `is_float(f.type)`, a function over a Field, `x->[f]`, and slice and array fields (`countof(x.[f])`, `x.[f][i]`, `x.[f][a:b]`, `typeof(x.[f][0])`). Load when reflection code stores a field in a variable, passes one to a function, or reads an element of a slice or array member."
---

# `fields(T)` is an array of `Field`

`Field` is `struct { char name[:]; U64 type; char doc[:]; }` (a `type` is a handle: `is_float(f.type)`, `f.type == F32`), defined once per file when its text has `fields(` or `Field`, or it imports a generic that reflects. `fields(T)[i]` is an ordinary value: store it, pass it to a function (which runs in the compiler), return it, compare its parts.

```smith
module fieldvals;

[[maybe_unused]] Bool is_floating(Field f) {            // ordinary C over a Field; it runs in the compiler
    return is_float(f.type);
}

F64 sum_floats(constexpr Type T, T *x) {
    F64 total = 0;
    for constexpr (Usize i = 0; i < countof(fields(T)); i++) {
        [[maybe_unused]] constexpr Field f = fields(T)[i];      // or `constexpr auto f`
        if constexpr (is_floating(f)) total += (F64)x->[f];     // x.[f], x->[f], typeof(T.[f]), is_float(f.type)
    }
    return total;
}

typedef struct {
    F32 a;
    I32 b;
    F64 c;
} Mixed;

export F64 demo() {
    Mixed m = {.a = 1, .b = 2, .c = 3};
    return sum_floats(Mixed, &m);                       // 4
}
```

`[[maybe_unused]]` is what the conventions' warnings need in a module (a function used only in a condition, a `Field` the dropped branch did not read). Exporting `is_floating` makes an importer fail on `Field` in the header (issue #181).

## Rules

- Fields are read by the compiler, so the function a condition calls must be defined in the file that instantiates the generic (an import's exported functions are visible: `smith-lang-constexpr`).
- A local `constexpr Field f = E;` stands for `E` in the conditions of its block; `constexpr Field g = first_float(fields(T)[0], fields(T)[1]);` works the same way. `fields(T)` alone is a `static constexpr Field[N]` (`const Field *all = fields(T);`). `f.name` in an ordinary function is a slice (`f.name.data`, `countof(f.name)`); `fields(T)[i].name` and `.doc` stay string literals (`f.doc` is a slice, empty when the field has no doc comment: `countof(f.doc) == 0`; a condition (`if constexpr`) reads `f.doc` as empty).
- **Slice and array members**: `countof(x.[f])`, `countof(p->xs)` and `countof(f.name)` are lowered in a generic's body; `typeof(x.[f][0])` is the element, in a condition (`is_float(typeof(x.[f][0]))`; a slice of pointers keeps its star: `char *names[:]` has `typeof(x.[f][0]) == char *`); `x.[f][i]`, `x->[f][i][j]` and `x.[f][a:b]` read an element or a cut as a value, checked like `x.name[i]` (the object is a name or a chain, `o->bag.[f][0]`). A slice of more dimensions needs every index (an error says so); `T xs[:]` locals in an instance stay raw (pass a cut to a function).
- `typeof(*p)` and `typeof(p->f)` of an `auto p` are types in a condition too. `constexpr Type k = f.type;` in a body is not built. `typeof(x->[f])` of a bit-field stored in a variable is C's own error.

Reflection: `smith-lang-reflection`.
