---
name: smith-lang-generic-entry
description: "A function defined in a generic's body, and the struct of its arguments: `static void entry(void *raw) {...}` inside `export void spawn(constexpr auto f, auto... rest)`, `typeof(rest)`, `a->[rest]...`, so a library makes a typed `void (*)(void *)` for each list of argument types. Load when a generic needs a callback or an entry function of its own, or on 'a function defined in a generic goes in the outermost block', '`typeof(rest)` is a struct of the arguments', 'function definition is not allowed here' in a generic."
---

# A function in a generic's body

A generic that hands work to a `void (*)(void *)` (a task, a thread, a callback table) needs one function for each list of argument types. Define it in the body: it is written once for the instance, at file scope, and its name is its address. The user of the generic writes no `void *` and no cast.

```smith
module later;

export typedef void (*Entry)(void *);

export typedef struct {
    Entry entry;
    alignas(16) u8 data[64];
} Slot;

export void defer(Slot *slot, constexpr auto f, auto... rest) {
    static_assert(sizeof(typeof(rest)) <= sizeof slot->data, "defer: too many arguments");
    static void entry(void *raw) {                      // a function of the instance, not a local one
        typeof(rest) *a = raw;                          // the struct of the arguments
        f(a->[rest]...);                                // f(a->_0, a->_1, ...)
    }
    typeof(rest) args = {rest...};
    *(typeof(rest) *)slot->data = args;
    slot->entry = entry;                                // its address: a plain function pointer
}
```

`later::defer(&slot, show, 3, (i64)4);` then `slot.entry(slot.data);` calls `show(3, 4)`. A call with other argument types is another instance with another `entry`.

## Rules

- **The function** starts with `static` and stands directly in the generic's body (its outermost block, not inside an `if`, a loop or a `for constexpr`; anywhere else is an error). At most 8. Its name in the body is the instance's function; it is no closure: it sees the generic's compile-time names (`T`, a `constexpr auto f`, `typeof(a)` of an `auto` parameter, `countof(rest)`, `typeof(rest[i])`, `if constexpr`, `for constexpr`) and no parameter or local of the generic (clang says `use of undeclared identifier`).
- **`typeof(rest)`** is a struct whose members `_0`, `_1`, ... have the types of the arguments, defined at file scope for the instance: use it in the body and in the functions. `{rest...}` fills it, `typeof(rest) *a = raw;` reads it. An empty `rest` is an error (a struct has a member).
- **`x->[rest]...`** and **`x.[rest]...`** (an argument of a call or an initializer) are `x->_0, x->_1, ...`: the members, one for each argument. A pattern: `g(a->[rest]...)`.
- It is plain C: a static function, a struct and a pointer. The generic's calls, `if constexpr` and the other forms in the nested function are written as in any instance.

Generics: `smith-lang-generics`. Argument lists: `smith-lang-rest`. The long reference: [knowledge/smith-generics.md](../../../knowledge/smith-generics.md) ("A function in a generic's body").
