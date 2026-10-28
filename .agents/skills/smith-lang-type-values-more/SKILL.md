---
name: smith-lang-type-values-more
description: "Functions over `Type` across modules and pointers in .smith: an exported `Type SliceOf(Type element, Usize count)` travels in the header (arena::SliceOf(F32, 2) runs in an importer), `typeof(*(t)nullptr)` for a pointer's pointee, `LeafOf(T)` as a generic's result type, and the limits (900 tokens, no `if constexpr` in the body, one level of pointee). Load when exporting or importing a function over `Type`, or on 'a pointer's pointee is known one level deep'."
---

# Functions over `Type`: modules and pointees

```smith
module shapes;

export Type SliceOf(Type element, Usize count) {   // no symbol, no declaration for C: the header records its tokens
    Type t = element;
    for (Usize i = 0; i < count; i++) t = t[:];
    return t;
}

static Type LeafOf(Type t) {                       // the pointee of a pointer type, else the type itself
    if (Pointer(t)) return typeof(*(t)nullptr);
    return t;
}

export LeafOf(T) first(constexpr Type T, T x) {    // a computed result type
    if constexpr (Pointer(T)) {
        return *x;
    } else {
        return x;
    }
}
```

## Rules

- **Exported functions over `Type` travel in headers.** The header records the tokens (`// smith-types:`, `// smith-generic: ... f`), and a private function over `Type` that an exported one calls too; an importer defines each, as a private function, after its last `import` line. So `arena::SliceOf(F32, 2)` runs in the compiler of any module that imports `std::arena`: in a `static_assert`, a `constexpr` initializer, a generic's result type or its `if constexpr`.
- A private (`static`) function over `Type` that an exported generic names in its computed result type (`same(T) keep(...)`) or calls in its body travels the same way, so every instance in an importing module finds it. An ordinary exported function (over numbers) is carried from the module's `.body` file, with no such limit (`smith-lang-constexpr`).
- **`t[:]`** is "slice of `t`" and `F32[2]` an array type value; `typeof(xs)` is the type of an expression. Each `[:]` puts a run-time dimension in front (`F32[:][:]` is depth 2; at most 8), and the result is a real handle (`==`, `sizeof`, `Struct(t)`, `t x;`). `t[1:2]` is an error.
- `sizeof(t)` is exact for the target: `long` is 4 with `--target=x86_64-pc-windows-msvc`, 8 on the host.
- **Limits:** the host must have found the import's header (a module found by its source is not carried yet); a function over `Type` is at most about 900 tokens, and its body has no `if constexpr`, `for constexpr` or call of a generic (an error at the import).
- **The pointee of a pointer type:** `typeof(*(t)nullptr)` in a function over `Type` is what pointer type `t` points to (C's own form, a cast of `nullptr`, dereferenced; `typeof` never evaluates it), and `typeof(expr)` is the type of any expression there. `LeafOf(T)` as a result type gives the struct for a pointer to a struct and the type itself for a scalar.
- **Limit:** a pointee is known one level deep: `typeof(*(t)nullptr)` of `T **` is a `T *` whose own pointee cannot be read (``a pointer's pointee is known one level deep``), and it is not `==` to a written `T *`. Not yet: `if constexpr` over a type function's type result.

Types as values: `smith-lang-type-values`; computed result types: `smith-lang-generics-more`. Reference: [knowledge/smith-generics.md](../../../knowledge/smith-generics.md) ("Types are values").
