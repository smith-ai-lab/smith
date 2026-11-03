---
name: smith-dev-syntax-values
description: "New syntax (#1428): lines and indentation blocks (`:`), `do:` block values, declarations (`let a = 3`, `let mut n: usize = 0`, `= undefined`, `let (q, mut r) = ...`, `const`), assignment, no shadowing, literals, pointers (`&x`, `p.*`) and conversions (`x.to(i64)`, `x.wrap(u8)`, `p.cast(*u8)`). Load when writing values, declarations or casts in the new syntax."
---

# Lines, declarations, pointers and conversions

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Lines and blocks

```smith
fn sign(x: i32) -> i32:
    if x < 0: -1 else if x == 0: 0 else: 1      // a one-line block follows its `:`

let total = price * count +
    shipping                                    // continues: the line ended with an operator

let area = do:                                  // a block as a value
    let w = right - left
    w * (bottom - top)                          // its last line is the value
```

- A `:` ending a line opens an indented block; code after the `:` on the same line is a one-line block. On one line `else:` belongs to the nearest `if`, so a nested `if` there needs parentheses.
- Indentation is 4 spaces; a tab is an error that `smith fmt` fixes. No semicolons. A line continues only when it ends with an operator or is inside an open `(`, `[` or `{`, where newlines and indentation are ignored. Comments are `//`.

## Declarations

```smith
let a = 3                       // a constant; the type is inferred (i32)
let b: f32 = 2.5                // the type written
let mut count: usize = 0        // a variable
let mut buf: [4096]u8 = undefined   // no value yet, no cost
let (q, mut r) = divmod(17, 5)  // destructuring; mut marks one name
let _ = f()                     // an unused result is an error: discard it explicitly
const TAU = 6.28                // must be computed at compile time
let IntPair = Pair(i32)         // an alias is a constant of type `type`
count += 1                      // assignment: only to a mut name, and a statement only
```

- Every declaration is `let PATTERN [: T] = expr`; one with no `=` is an error. A read of an `undefined` name before a write is a compile error where the checker can see it; debug builds fill it with a pattern.
- `x = e` without `let` always assigns. Shadowing is an error ("assign with `=`, or choose another name").
- A bare integer literal is `i32` and a float literal `f64`, unless a type is written or expected.

## Pointers and conversions

```smith
let mut p = Point{x = 1, y = 2}
let q = &p                      // *mut Point, because p is mut; &x of a constant is *T
q.x += 1                        // fields through a pointer: no ->
let n = q.x.to(i32)             // a value conversion, checked: a lossy one traps
let low = n.wrap(u8)            // keep the low bits, on purpose
let bytes = q.cast(*mut u8)     // reinterpret a pointer: rare and unsafe
```

- `to`, `wrap` and `cast` are built-in functions, so also method calls on any value. `as`, prefix `*p` and `(T)x` are errors whose fixes write these. There is no `&mut` and no `->`.

## Not settled yet

`[_]` lengths and `void *`.
