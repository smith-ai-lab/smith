---
name: smith-dev-syntax-arrays
description: "New syntax (#1428, #2482): index comprehensions `for i, j:` over array shapes (no `in`), matrix multiply, transpose, dot product, sizes as bracket parameters (`n: usize`, `const n: usize`), run-time results in the current allocator, `array.zeros`, `ones`, `filled_like`, field-by-field `==`, no user-defined operators, no struct ordering. Load when writing array math, comparing structs, or reaching for operator overloading."
---

# Array math, `==` and operators

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the design is [index-comprehensions](../../../knowledge/index-comprehensions.md) (#2482). Not built yet: the forms below are decided, and the build tickets are listed on #2482.

## Index comprehensions

`for x in seq:` walks values; `for i:` with no `in` is an index over an array's shape, its range taken from how `i` is used. Several indices: `for i, j:`.

```smith
c := for i, j: sum(for k: a[i, k] * b[k, j])   # matrix multiply, c: [n, p]f64
t := for i, j: a[j, i]                          # transpose
d := sum(for i: xs[i] * ys[i])                  # dot product
```

- An index used on several arrays needs equal sizes there: a compile error for sized arrays, a precondition for `[]T` (`smith prove` shows it, or the function checks it at entry).
- A result with a run-time size is built in the current allocator and the function gets `.alloc`; a fused reduction allocates nothing. The index form is the one the compiler fuses.
- Sizes are values in brackets, inferred from the arguments; `const n: usize` requires compile time. A sized array passes where `[,]T` is expected.

```smith
fn matmul[T: num, n: usize, m: usize, p: usize](a: [n, m]T, b: [m, p]T) -> [n, p]T:
    for i, j: sum(for k: a[i, k] * b[k, j])

z := matmul(x, y)   # x: [3, 4]f64, y: [4, 2]f64: z on the stack, a size mismatch is a compile error
w := matmul(u, v)   # u, v: [,]f64: sizes read at run time, w in the current allocator (.alloc)
```

`array.zeros(f64, [3, 3])`, `array.ones` and `filled_like(a, value)` build on `array.filled(T, [a, b], value)`. Index sets (Dex) are not designed (#2505).

## Operators and `==`

Operators are language-defined only (numbers, `vec(T, N)` element-wise, pointers, `<` `<=` `>` `>=` on payload-free enums); no overloading, no traits. Bigint and rational use named functions (`a.add(b)`).

```smith
p == Point(x = 1, y = 2)                # field by field, no user code
sort(people) fn(a, b): a.age < b.age    # no struct ordering: the order is given at the call
```

- `==` works on numbers (IEEE for floats), bool, enums (tag, then payload), pointers (address), fixed arrays, tuples and structs whose fields all have it. Slices have none, so neither does a struct holding one; the error names `slice.equal`. No struct `<`, no field-by-field `+`.
- Hashing, serialization, printing, cloning and deep equality are library functions over `fields(T)`; a container that needs another rule takes it as a parameter: `map.init(K, V; eq = my_eq)`.
