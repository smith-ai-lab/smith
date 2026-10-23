---
name: smith-lang-arrays
description: "Array parameters in a .smith module: `i32 xs[4]` keeps its size (checked indexes, shapes and uses, countof(xs)), `char **argv` for main, why `T xs[]` is an error. Load when a module has a parameter written as an array, on 'array parameter', 'an array parameter needs its size', 'is out of range for the array parameter' or 'pass an array of that shape'."
---

# Array parameters

This skill ships in the smith plugin, from `.agents/skills/smith-lang-arrays/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Overview: `smith-lang`.

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
