---
name: smith-lang-arrays
description: "Run-time arrays in .smith: `i32 xs[:]`, a slice (pointer and count) in a parameter, result, local, global or field; `countof(xs)`, `xs[i]`, `xs[a:b]`, `p[a:b]`, `char s[:] = \"abc\"`, `typeof(i32[:])`, checked in debug and release, and a slice passed to a generic. Load when a module has `[:]` or `xs[a:b]`, or on 'a slice has no step', 'an index is never negative', 'a pointer is no slice', 'cannot slice this expression'."
---

# Slices: `T xs[:]`

A C pointer carries no length; a slice is a pointer and the number of elements after it. `[:]` is invalid C, so it works in any `.smith` file.

```smith
module slices;

export i32 sum(const i32 xs[:]) {                            // C: i32 sum(const i32 *xs, usize n)
    i32 total = 0;
    for (usize i = 0; i < countof(xs); i++) total += xs[i];  // countof(xs) is a usize
    return total;
}

export i32 demo() {
    i32 raw[5] = {1, 2, 3, 4, 5};
    i32 all[:] = raw;               // a fixed array converts: count 5
    i32 mid[:] = all[1:4];          // a view that shares the elements: 2, 3, 4 (xs[:b], xs[a:] too)
    i32 *p = raw;
    i32 view[:] = p[2:5];           // a pointer becomes a slice by saying how many: the one unsafe spot
    char word[:] = "abc";           // count 3, no terminating zero (`char s[_] = "abc"` is count 4)
    return sum(mid) + sum(view) + (i32)countof(word);
}
```

## Fixed arrays: `[3]` and `[_]`

In a module an array's brackets say what it is, and a bare `[]` is an error.

`i32 xs[3]` has its size written; `i32 xs[_] = {1, 2, 3}` takes it from the initializer (C's `[]`; `char s[_] = "abc"` is 4, `i32 m[_][3] = {...}` and `(i32[_]){1, 2}` work); `i32 xs[:]` is a slice.

`[_]` needs an initializer and is only the outermost dimension. The errors: `[]` with an initializer (write `[_]` or `[:]`), a compound literal `(T[]){...}` (write `(T[_]){...}`), `extern T xs[]` (write the size), a flexible array member (use a slice field `T xs[:]`), a parameter `T xs[]` (`smith-lang-array-params`). A `.c` file or a `.smith` file with no `module` line is C's, unchanged.

## Rules

- `xs[i]` and `xs[a:b]` are checked in debug and release: a failure is `contract::assert`'s report with the line and the stack. The conventions link the trace library when a source has `[:`; a hand-written `smith_program` names `smith_library(b, "trace")`.
- **A proof removes a check, and nothing else does.** After `smith prove`, `smith build release` and `fast` (not debug) leave out the checks `.smith/proved/<source>.pts` lists, for exactly the source text and smith that proved them; an edit of the source or of a callee file it relied on, a missing file or another smith keeps every check; debug (-O0) always keeps every check, as a safety net against a prover bug. `SMITH_PROVED_REPORT=1 smith cc -c f.smith -O2 ...` prints `N removed (proved), M kept`. A function whose check is not proved stays checked (`smith-prove`).
- `countof(xs)` of a local or member FIXED array (`u32 xs[4]`) is its outer count (`sizeof` over the element's, never a C2y warning), in a plain function and in a generic's instance; of a pointer it is clang's error.
- A string literal is a slice wherever a slice is expected: an argument, a member in a struct initializer (`{{"unsigned", "u32"}, ...}`), an element of an array of slices (`Text names[3] = {"a", "bb", "ccc"}`) and the two arms of `flag ? "a" : "b"`.
- Empty is count 0 with a null pointer. `xs[a:b]` needs `a <= b <= countof(xs)`; `p[a:b]` needs `a <= b` and `p` pointing at `b` elements; it works in a generic's body for a parameter `T *p` (`export T[:] window(constexpr type T, T *p, usize a, usize b) { return p[a:b]; }`: a slice of `T` per instance).
- A slice is a pointer only as an argument of a `c::` function (`c::strncmp(word, "abc", countof(word))`).
- A slice is a parameter, a result (`i32[:] f(...)`, or `typeof(i32[:]) f(...)`: the bare form is the same), a local, a global or a field. `arena::push_slice(&a, T, n, m...)` makes one out of an arena (`push_slice(&a, f32, n, m)` is `f32[:][:]`; `smith-arena`): bind it to a `T xs[:]` before indexing.
- **Visit every element** with `for (i32 x : xs)` (`smith-lang-for`) instead of `for (usize i = 0; i < countof(xs); i++)`.
- **Qualifiers** (`const`, `restrict`): `smith-lang-slice-qualifiers`. **A pointer to a slice**: `smith-lang-slice-pointers`. **More dimensions** (`f32 xs[:][2]`): `smith-lang-array-dims`. **A parameter written `i32 xs[4]`**: `smith-lang-array-params`.
- **A slice goes to a generic** (`sum(xs)` with `auto xs`, `test::check_equal(a, b)`; a generic may return one: `typeof(xs) tail(auto xs)`). There `countof(xs)`, `xs[i]`, `&xs[0]` and `xs[a:b]` work as outside (one dimension) and `if constexpr (is_slice(typeof(xs)))` asks the type (true for `T[:]` and more dimensions; false for a pointer and a fixed array). `format` over slices is not built.
- Underneath: `struct smith_array_i32_n { int32_t *data; size_t count[1]; }`, so C calls an export with `(struct smith_array_i32_n){.data = p, .count = {n}}`.

## Errors

- constant negative index: ``an index is never negative: the last element is `xs[countof(xs) - 1]` ``
- ``a slice has no step: `xs[a:b]` takes the elements a to b - 1``
- ``a pointer is no slice: say how many elements it points at, `p[0:n]` `` (also slicing a pointer or an array with one bound)
- a slice where a pointer is wanted is clang's own ``initializing 'i32 *' with an expression of incompatible type 'struct smith_array_i32_n'``: pass it to a slice parameter or a `c::` function
- a slice across rows: `smith-lang-array-dims`

A slice may be the `some` of an `option`: a generic returns `option(slice_of(T, 1))`, a caller writes `typedef i32 Row[:]; typedef option(Row) MaybeRow;` (`smith-option`).
