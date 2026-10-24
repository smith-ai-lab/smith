---
name: smith-lang-arrays
description: "Arrays in a .smith module: run-time arrays `i32 xs[:]` (a slice: pointer and count; `countof(xs)`, `xs[i]`, `xs[a:b]`, `p[a:b]`, `char s[:] = \"abc\"`, `typeof(i32[:])`, checked in debug and release) and array parameters `i32 xs[4]` that keep their size (checked indexes, countof(xs)), `char **argv` for main, why `T xs[]` is an error. Load when a module has `[:]`, `xs[a:b]`, a parameter written as an array, `countof`, or on 'a slice has no step', 'an index is never negative', 'a pointer is no slice', 'cannot slice this expression', 'more than one dimension is not built yet', 'array parameter', 'an array parameter needs its size' or 'pass an array of that shape'."
---

# Arrays

This skill ships in the smith plugin, from `.agents/skills/smith-lang-arrays/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Overview: `smith-lang`.

## Run-time arrays: `T xs[:]`

A C pointer carries no length. `i32 xs[:]` is a pointer and the number of elements after it, in a parameter, a result (`typeof(i32[:]) f(...)`), a local, a global and a struct field. It is invalid C, so it works in any `.smith` file.

```
i32 sum(i32 xs[:]) {
    i32 total = 0;
    for (usize i = 0; i < countof(xs); i++) total += xs[i];    // countof(xs) is a usize
    return total;
}
i32 raw[5] = {1, 2, 3, 4, 5};
i32 all[:] = raw;               // a fixed array converts: count 5
i32 mid[:] = all[1:4];          // a view that shares the elements: 2, 3, 4  (xs[:b], xs[a:] too)
i32 *p = raw;
i32 view[:] = p[2:5];           // a pointer becomes a slice by saying how many: the one unsafe spot, visible in the code
char word[:] = "abc";           // count 3, no terminating zero; `char s[] = "abc"` is C's (a char[4], count 4 as a slice)
c::strncmp(word, "abc", countof(word));   // a slice is a pointer only as an argument of a c:: function
```

- `xs[i]` and `xs[a:b]` are checked in debug and release builds, not fast: a failure is `contract::assert`'s report with the line and the stack. The program links the trace library with no line in its build file: the conventions (`smith_app`, `smith_tests`) find a `[:` or `:]` in a `.smith` source and link it (a hand-written `smith_program` names `smith_library(b, "trace")`). A constant negative index, a step (`xs[a:b:2]`), slicing a pointer or an array with one bound, passing a pointer where a slice is wanted, and `[:][:]` (more dimensions are #105; rows of different lengths are a struct) are errors.
- Empty is count 0 with a null pointer. `xs[a:b]` needs `a <= b <= countof(xs)`; `p[a:b]` needs `a <= b` and `p` pointing at `b` elements.
- Underneath: `struct smith_array_i32_n { int32_t *data; size_t count[1]; }`, defined where used and in a generated header, so C writes `(struct smith_array_i32_n){.data = p, .count = {n}}` to call an export. The array expression of `xs[i]` and `xs[a:b]` runs once, so `get()[i]` calls `get` once.
- `arena::push_slice(&a, T, n)` is a `T[:]` out of an arena (`smith-arena`); bind it to a `T xs[:]` before indexing it.
- Not yet: `format` and `check_equal` over slices and `T xs[:][2]`. `smith prove`, the debugger and the editor know `[:]` (`smith-prove`, `smith-debug`, `smith-lsp`).

## Array parameters: `i32 xs[4]`

In C a parameter `i32 xs[2]` is a pointer: the `2` is dropped. In a file with a `module` line it is kept; a file with no `module` line means what it means in C (`xs[]`, `xs++`, `sizeof(xs)` and all). Only parameters change, and every change is an error, never a new meaning.

```
module rows;

export i32 pick(i32 xs[4], usize i) {      // 4 objects; the header says `int32_t xs[4]`
    c::smith_requires(i < 4);                // all `smith prove` needs: xs[i] is in the array
    return xs[i];
}
export usize cells(f32 m[3][2]) { return countof(m) * countof(m[0]); }   // 3 * 2
```

- `countof(xs)` is the size (`countof(m[0])` the next dimension), a `usize` the code writes from the parameter's own size.
- A constant index out of range (`xs[3]` for `xs[2]`, `xs[-1]`, `m[0][2]`) is a compile error.
- An argument to a module's function must be an array of the same dimensions and elements (`const` may be added): another shape, a pointer or `nullptr` is an error. A C function (`c::memcpy(xs, ...)`) takes the array as a pointer, as in C.
- `xs++`, `xs += 1` and `xs = p` are errors, and so is `sizeof(xs)` (the pointer's size): the message names `countof(xs)`.
- `i32 xs[]` is an error, `char *argv[]` included: write `i32 *xs`. `main` is `i32 main(i32 argc, char **argv)`.
- Pointer behaviour is a pointer: `i32 *xs`. There is no cast from a pointer to an array parameter.
- Underneath it is the C pointer, same ABI. The compile writes `xs[static 4]`, so clang and `smith prove` know it is not null and has 4 objects; a function pointer's parameters and the parameters of a plain `.smith` file are not changed.
