---
name: smith-lang-array-dims
description: "More dimensions in .smith: `F32 xs[:][2]`, `xs[:][:]`, `I32 c[:][3][:]` (up to 8, one contiguous row-major block), a fixed array converting, a row `xs[i]`, `xs[i][a:b]`, `xs[a:b]` of rows, countof of each dimension. Load when a module has two `[:]` or a `[:]` before a number, or on 'a slice of rows has no strides', 'the array has not the shape of the slice'."
---

# More dimensions: `F32 xs[:][2]`, `xs[:][:]`

Each dimension is `[:]` (a size known at run time) or a number, in one contiguous row-major block. The first is `[:]`; at most 8.

```smith
module grids;

export F32 demo() {
    F32 raw[4][2] = {{1, 2}, {3, 4}, {5, 6}, {7, 8}};
    F32 pairs[:][2] = raw;          // a fixed F32[4][2] converts: count 4 (a number must match the array's own)
    F32 grid[:][:] = raw;           // counts {4, 2}
    F32 x = pairs[2][1];            // 6; countof(pairs) is 4 and countof(pairs[0]) is 2
    F32 row[:] = pairs[1];          // a row is again a [:], sharing the elements
    F32 part[:] = pairs[3][0:1];    // xs[i][a:b] slices within a row
    F32 tail[:][2] = pairs[1:4];    // xs[a:b] takes rows: a view of the same shape
    return x + row[1] + part[0] + (F32)countof(grid) + (F32)countof(grid[0]) + tail[0][0];
}
```

## Rules

- `xs[i][j]` is checked against each dimension; `countof(xs)` is the first dimension, `countof(xs[0])` the next.
- `F32 t[:][:][:]` is a tensor; `I32 c[:][3][:]` a mix. Slicing in a generic: a row is taken in the caller, and `x.[f][i][j]` needs every index (`smith-lang-reflection`).
- Underneath: `struct smith_array_f32_n_2 { float *data; size_t count[1]; }`, `count[k]` for the k `[:]`, the fixed sizes in the name (`_n` run-time, `_2` a number); `typeof(F32[:][2])` is the type.
- Not yet: a fixed dimension that is not a number, a first dimension that is a number (`F32 xs[2][:]`).

## Errors

- ``a slice of rows has no strides``: `xs[a:b][c:d]` slices an inner dimension across rows; slice each row. A fixed array of more than one dimension is not sliced either: convert it to a slice first.
- ``the array has not the shape of the slice``: a number in the slice's type that is not the array's own.

Slices: `smith-lang-arrays`.
