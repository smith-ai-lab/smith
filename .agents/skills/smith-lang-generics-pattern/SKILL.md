---
name: smith-lang-generics-pattern
description: "A parameter written as a pattern in .smith: `void push(List(constexpr Type T) *xs, T item)` takes any instance of the type function `List` and binds T from the argument's type; holes with a concept (`constexpr Num T`), several holes, nested patterns. Load when a parameter is a type function's call with `constexpr` in it, or on 'does not match' and 'is not an instance of'."
---

# A parameter written as a pattern

A type function (`Type List(Type T) { return struct {...}; }`) makes a struct for each argument. A generic that takes any of them names the type function with a hole in place of the argument:

```smith
module main;

import <stdio.h>;

Type List(Type T) {
    return struct {
        T *items;
        usize count;
    };
}

void push(List(constexpr Type T) *xs, T item) {
    xs->items[xs->count] = item;
    xs->count += 1;
}

i32 main(void) {
    i32 storage[4] = {};
    List(i32) xs = {.items = storage};
    push(&xs, 3);
    c::printf("%zu\n", xs.count);
    return 0;
}
```

## Rules

- **A hole is `constexpr Type T`**; the call gives it no argument: the argument's type is looked up among the instances of `List` (each records its type function and arguments), `T` is bound (`I32`), and the rest of the signature uses it (`T item`, a result `T`). One instance of `push` for each binding, plain C.
- **A hole may take a concept** (`List(constexpr Num T)`): an instance whose argument is not of it is an error at the call, ``` `const char *`, the argument of `Box`, is not `arithmetic` ```.
- **Several holes and nesting**: `Pair(constexpr Type A, constexpr Type B) *pair`, `Option(Box(constexpr Type T)) maybe`. An argument that is no hole is compared with the instance's: `Pair(constexpr Type A, F64) pair` takes a `Pair(i32, f64)` only.
- **The parameter is by value or by pointer** (`List(constexpr Type T) xs`, `... *xs`, `const` first); any other declarator is not a pattern.
- **A non-instance is an error at the call**: ``` `i32`, the argument of `last`, does not match `List(constexpr type T)`: it is not an instance of `List` ```, with the note of the parameter.
- **An exported pattern works for an importer**: the header carries a `// smith-einstance:` line for each instance a module's exports name, so `lists::push(&xs, 1)` finds the instances of a list `lists::make()` returned.
- A generic function is not `static`; a hole name is a type of the whole signature and body, as `constexpr Type T` is. `auto` stays for a type with no type function behind it (`smith-lang-generics`).
