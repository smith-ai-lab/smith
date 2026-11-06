---
type: design
title: Index comprehensions, == on values and no user-defined operators
description: How smith does array math without operator overloading or traits (for i, j: over array shapes, sizes as bracket parameters, run-time results in the current allocator), why == compares values, and what stays a library function over fields(T).
tags: [syntax, arrays, linear-algebra, operators, design]
---
# Index comprehensions

Issue #2482, decided by the owner on 2026-11-05 and 2026-11-06; for after the switch-over (#1631), nothing here blocks it. smith should be "fantastic for linear algebra" without operator overloading or traits.

## The forms

`for x in seq:` walks the values of any sequence. `for i:` with no `in` is an index over an array's shape: its range comes from how `i` is used (Dex-style); several indices are `for i, j:`. Owner: "So having the in makes it clear right? If you don't have it, then it is index based, otherwise it is ranged based?"

```smith
c := for i, j: sum(for k: a[i, k] * b[k, j])   # matrix multiply, c: [n, p]f64
t := for i, j: a[j, i]                          # transpose
d := sum(for i: xs[i] * ys[i])                  # dot product
```

Owner: "c := for i, j: sum(for k: a[i, k] * b[k, j]) is clearly more beautiful." The index form is the one the compiler fuses (and may later parallelise or map to a GPU grid; design only).

## Sizes

An index used on several arrays (`k` in `a[i, k]` and `b[k, j]`) needs equal sizes along those dimensions. Sizes are ordinary values in brackets, inferred from the argument shapes like any generic parameter, so the signature links the shapes (owner, 2026-11-06, "Yeah I think that sounds right"):

```smith
fn matmul[T: num, n: usize, m: usize, p: usize](a: [n, m]T, b: [m, p]T) -> [n, p]T:
    for i, j: sum(for k: a[i, k] * b[k, j])

z := matmul(x, y)   # x: [3, 4]f64, y: [4, 2]f64: n, m, p constant, z: [3, 2]f64 on the stack, a mismatch is a compile error
w := matmul(u, v)   # u, v: [,]f64: n, m, p read from the shapes at run time, w built in the current allocator (.alloc)
```

- `n: usize` in brackets is constant when the shapes are and a run-time value otherwise; `const n: usize` requires compile time (a GPU kernel).
- A sized array passes where `[,]T` is expected, as `[4]T` passes for `[]T`.
- A size shared across parameters (`m`) is a compile-time check for constant shapes and a precondition for run-time ones: `smith prove` shows it, or the function checks it at entry.
- A run-time mismatch is a checked failure the compiler writes from the signature: it traps like an out-of-range index, with a message naming the size and both values (`matmul: m: a has 4, b has 5`). `smith prove` reports it as a precondition and removes the check where it proves the sizes equal. There is no `contract.requires` to write and no `Result` (owner, 2026-11-06, on question 2's addition: "Yeah I think that sounds right"; the lead chose this reading for question 3).
- Shapes go no further than values: index sets (Dex) are neither designed nor promised (#2505).

## Where a run-time result lives

An index comprehension with a run-time size builds its result in the current allocator, as today's `for` expression does, and the function gets `.alloc`; fused reductions (`sum(for k: ...)`) allocate nothing. Owner: "Definitely a. That is why we have the effect system".

## Constructors

`array.filled(T, [a, b], value)` stays the primitive (#2496; [array-filled](array-filled.md)): the type first, no default type, the shape written as the type writes it and constant. On top of it, for numeric types: `array.zeros(f64, [3, 3])`, `array.ones(f64, [3, 3])`, an identity matrix (its name and shape argument are settled in its build ticket) and `filled_like(a, value)` (shape and type taken from `a`). Owner, 2026-11-06: "Okay, I'm sold."

## Operators and equality

- No user-defined operators and no traits. Operators stay language-defined: numbers, `vec(T, N)` (element-wise), pointers, and `<`, `<=`, `>`, `>=` on payload-free enums in declaration order. Element-wise array math is an index comprehension. Bigint and rational use named functions (`a.add(b)`). Whether complex numbers become built-in is decided separately. Owner: "Then we don't need operator overloading!"
- `==` compares **values** for every type, with no user code: numbers (IEEE for floats), bool, enums (tag, then payload), fixed arrays, tuples, structs (field by field), and slices, which compare their length and then each element, so `name == "smith"` compares text. A pointer's value is its address, so `==` never follows a pointer. Owner, 2026-11-06: "Yeah == should be the way to compare things. Then we don't need these special equal functions". "Same memory" is written out: `a.data == b.data and countof(a) == countof(b)`. There is no `slice.equal`, `mem.equal` or `eq.equal`.
- No struct ordering and no field-by-field `+`. Sorting takes the order at the call: `sort(people) fn(a, b): a.age < b.age`.
- Everything else is a library function over `fields(T)`, written once: hashing, serialization (`@serial.rename`, `@serial.skip`), printing, cloning. A container that needs another rule takes it as a parameter with the reflection version as default: `map.init(K, V; eq = my_eq)`.
- Uniform call syntax and the lookup in the type's module stay as they are.

## Interactions

Effects (a built array lives on the stack when sized, in the allocator otherwise); generics (`sum` and the index form over any `num`); autodiff (a comprehension must differentiate through Enzyme like the loop it lowers to); contracts and `smith prove` (size equality for `[]T` as an obligation); `smith lsp` shows the inferred range of an index; C interop none, the lowering is a plain loop.

## Not built yet

Nothing above is implemented. The build tickets are filed from #2482.
