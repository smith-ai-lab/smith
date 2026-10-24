---
name: smith-lang-slice-qualifiers
description: "Qualifiers on a slice as C has them on a pointer: `const u8 xs[:]` and `u8 const xs[:]` (read-only elements), `u8 xs[const :]` (the slice itself fixed), `u8 xs[restrict :]` (no other pointer overlaps), `const u8 src[const restrict :]`, a `const char s[:] = \"abc\"` that points into the literal, const slices in generics and in more dimensions. Load when a .smith file has `const`, `restrict` or `static` inside `[ :]`, a const slice, or on 'discards qualifiers', 'read-only variable is not assignable', 'has no meaning for a slice' or 'a qualifier goes in the first bracket'."
---

# Qualifiers on a slice

This skill ships in the smith plugin, from `.agents/skills/smith-lang-slice-qualifiers/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Slices: `smith-lang-arrays`; generics: `smith-lang-generics`.

C's rules for pointers, carried over unchanged. The element's qualifiers go with the element type, the slice's own go inside its brackets, after C99's `int a[const 10]` (`int *const a`) and `int a[restrict 10]`.

```
usize sum(const u8 xs[:]);                       // west const: the elements are read-only; `u8 const xs[:]` is the same type
void copy(u8 dst[restrict :], const u8 src[const restrict :]);   // no other pointer or slice reached by the call overlaps dst
usize fixed(u8 xs[const :]);                     // the slice (pointer and count) can't be reassigned or re-cut; the elements are writable
const char word[:] = "abc";                      // points into the literal, count 3, no copy (`char word[:] = "abc"` is the same today)
const f32 m[:][:] = grid;                        // the element qualifier is the innermost element's
f32 rows[const :][2] = grid;                     // the bracket qualifiers are the first bracket's: one pointer, one block
```

- **Conversions follow C's pointer rules.** `u8[:]` to `const u8[:]` is implicit (a call, `=`, an initializer, a return); the reverse is an error ("converting a slice of `const u8` to a slice of `u8` discards qualifiers"). A cut `xs[a:b]` of a `const u8[:]` keeps the `const`. `restrict` goes either way, as in C. A write through a const element and `r = ...` of a `[const :]` slice are clang's own errors.
- **`[static :]` and `[volatile :]` are errors** (they mean nothing for a slice: `volatile` goes on the element, `volatile u8 xs[:]`). A qualifier in a later bracket (`f32 m[:][const :]`) is an error too: only the outermost bracket may carry qualifiers, as in C (the rows are views with no pointer of their own).
- **Generics.** A `const u8[:]` argument passes as itself: `typeof(xs)`, `is_slice` and `fields(T)` keep the element's `const`, and `stats::count(const_slice)` is a different instance from `stats::count(plain_slice)`. The slice's own `const` and `restrict` are no part of the type a value holds (`typeof(dst)` of `u8 dst[restrict :]` is `u8[:]`); a generic that returns `typeof(xs)` of a `restrict` slice and cuts it works. A call in a generic's body converts a slice parameter passed as a bare name as an ordinary call does (`const` added, `restrict` added or dropped; dropping `const` is the error above); other conversions in a generic's body (an initializer, an assignment, a return, an argument that is not a bare parameter) are not done.
- **Underneath:** the struct for `const T` has a `const T *data` (`struct smith_array_const_u8_n`); `[const :]` makes the variable `const`; `[restrict :]` makes the data pointer `__restrict` (clang's spelling that C++ headers read too) in a struct of its own, `struct smith_array_restrict_u8_n`, which a call converts to and from the plain one. The type is a slice by value (two integers), so the IR has no `noalias`; `restrict` is in the debug info (`DW_TAG_restrict_type` on the struct's `data`).
- `smith fmt` writes the brackets today as `[const:]` and `[const restrict:]` (no space before the colon; `[const :]` is how the examples above are written, and the formatter will keep that space later), and keeps `char const xs[:]` or `const char xs[:]` as written. Neither form is valid C, so a file stays a `.smith` file.
