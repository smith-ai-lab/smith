---
name: smith-lang-array-results
description: "A function that returns an array in .smith: `Key[2] pair(Key k)`, `Key[n] split(Key k, constexpr usize n)`, a slice result written bare `i32[:] tail(i32 xs[:])`, `Key keys[2] = split(key, 2);`, `return array;` and `return {a, b};`, the struct underneath (`struct smith_array_Key_2 { Key data[2]; }`) for C. Load when a function result is written `T[3]`, `T[n]` or `T[:]`, or on 'the size of an array a function returns', 'the array has another size than the call's result', 'the array returned has another size'."
---

# A function returns an array: `T[3] f()`

C cannot return an array, so a function that makes a fixed number of values fills an out parameter. In a module the result is written `T[n]` before the name; `n` is a number, or a `constexpr` parameter of a generic. A slice is written the same way, `T[:]` (no `typeof`). Both are invalid C (a name between a `]` and a `(`), so no C file changes meaning.

```smith
module keys;

export struct Key {
    u64 high;
    u64 low;
};

export Key[n] split(Key key, constexpr usize n) {      // each n is an instance with its own struct
    Key out[n];
    for constexpr (usize i = 0; i < n; i++) {
        out[i].high = key.high + i;
        out[i].low = key.low * 2 + i;
    }
    return out;
}

export i32[2] twin(i32 x) {
    return {x, x + 1};
}

export i32 first_low(Key key) {
    Key halves[2] = split(key, 2);                  // an array is initialized from the call
    return (i32)halves[0].low;
}
```

## Rules

- `return array;` copies the elements of an array of the same size; `return {a, b};` is the list. Another size is a compile error.
- `T xs[N] = call();` declares `xs`, then writes its elements from the call's (the call runs once); `N` is a number and must be the call's size, else ``the array has another size than the call's result``. A local in a function body, not `const` (``an array initialized from a call is not `const` ``).
- A generic's `T[n]` takes `T` too (`export T[n] fill(constexpr type T, T v, constexpr usize n)`); `n` is a `constexpr usize` parameter given a number.
- Each element of a returned array is its own value (a linear `Key` is used once per element).
- Underneath: `struct smith_array_<element>_<n> { <element> data[n]; }`, named like a slice's struct (`smith_array_<element>_n`) with the size in place of `n`, and returned by value. An export's header defines it (guarded `SMITH_ARRAY_...`), so C reads `r.data[i]`; a generic is not callable from C.

## Errors

- ``the size of an array a function returns is a number, or a `constexpr` parameter of a generic``: `i32[limit] f()` with a variable.
- ``the array has another size than the call's result``: `i32 three[3] = two(1);`.
- ``the array returned has another size than the function's result``: `return two;` in a `i32[3]` function.

See `smith-lang-arrays` (slices), `smith-lang-generics` (`constexpr usize`).
