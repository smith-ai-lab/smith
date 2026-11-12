---
name: smith-dev-syntax-values
description: "New syntax (#1428): lines and indentation blocks (`:`), `#` comments and `##` doc comments, `do:` block values, declarations (`a := 3`, `mut n := 0`, `count: usize = 0`, `= undefined`, `(q, mut r) := ...`, `const`), assignment, no shadowing, literals, pointers (`&x`, `p.*`) and conversions (`x.to(i64)`, `x.wrap(u8)`, `p.cast(*u8)`). Load when writing values, declarations, comments or casts in the new syntax."
---

# Lines, declarations, pointers and conversions

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Lines, blocks and comments

```smith
## The sign of x: -1, 0 or 1.
fn sign(x: i32) -> i32:
    if x < 0: -1 else if x == 0: 0 else: 1      # a one-line block follows its `:`

total := price * count +
    shipping                                    # continues: the line ended with an operator

area := do:                                     # a block as a value
    w := right - left
    w * (bottom - top)                          # its last line is the value
```

- A `:` ending a line opens an indented block; code after the `:` on the same line is a one-line block. On one line `else:` belongs to the nearest `if`, so a nested `if` there needs parentheses. Write 4 spaces a level. The parser accepts any consistent indentation (a block is deeper than the line that opens it, and a dedent lands on an open block), and `smith fmt` re-indents to 4. A tab is an error. No semicolons. A line continues only when it ends with an operator or a comma, when it starts with an operator that cannot begin a statement (`&&`, `||`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `/`, `%`, `<<`, `>>`, `|`, `^`; not `-`, `+`, `*`, `&` or `(`), or is inside an open `(` or `[`, where newlines and indentation are ignored. `|>` does not exist yet, so the rule does not name it (#3240).
- A comment is `#` and a space; `##` is a doc comment, read by the generated header and by reflection (`f.doc`). `//` is gone, and `#word` with no space (`#if`, `#include`) is an old directive: an error with a fix-it.

## Declarations

```smith
a := 3                          # a constant; the type is inferred (i32)
mut count: usize = 0            # a variable, its type written
mut buf: [4096]u8 = undefined   # no value yet, no cost
(q, mut r) := divmod(17, 5)     # destructuring; mut marks one name
_ := f()                        # an unused result is an error: discard it explicitly
const TAU = 6.28                # must be computed at compile time
IntPair := Pair(i32)            # an alias is a constant of type `type`
count += 1                      # assignment: only to a mut name, and a statement only
```

- Every declaration is `PATTERN := expr` or `NAME: T = expr`, with `mut` before a name for a variable; one with no value is an error. A read of an `undefined` name before a write is a compile error where the checker can see it; debug builds fill it with a pattern. The owner chose this on 2026-11-05 (#2316): "Okay yeah what if we did no shadowing and := and got rid of let"; for trailing blocks, "if for trailing lambda we required parenthesis even with no parameters"; and for `if let`, among the options, "if x := opt: (Recommended)".
- `x = e` always assigns; `:=` always declares. Redeclaring a name in scope is the shadowing error ("assign with `=`, or choose another name"). A local, a parameter or a loop name may not be the name of a module the file imports either, the last part of its path or its `as` name (`array: Ty = w.ty` beside `import std.array`): "`array` hides the module `std.array` that this file imports; shadowing is an error: choose another name" (owner, 2026-11-10, #3026: "yeah it should be a shadowing error please"). A statement starting `name:` is a typed declaration, never a block call. Globals at the top level are written the same way (`x := v`, `mut x := v`, `x: T = v`); `const` is unchanged. A bare integer literal is `i32` and a float literal `f64`, unless a type is written or expected.
- Until #2316 merges, the parser still takes `let` with a warning and a fix-it that writes these forms.
`==` compares values and operators are never user-defined: [smith-dev-syntax-arrays](../smith-dev-syntax-arrays/SKILL.md).

## Pointers and conversions
```smith
mut p := Point(1, 2)
q := &p                         # *mut Point, because p is mut; &x of a constant is *T
q.x += 1                        # fields through a pointer: no ->
n := q.x.to(i32)                # a value conversion, checked: a lossy one traps
low := n.wrap(u8)               # keep the low bits, on purpose
bytes := q.cast(*mut u8)        # reinterpret a pointer: rare and unsafe
```

- A conversion that cannot lose information is implicit (owner, 2026-11-06, #2531), in assignments, arguments, returns, comparisons and arithmetic (the narrower operand widens): `u8` to `u16`/`u32`/`u64`/`i16`, `i8` to `i16`/`i32`/`i64`, `u16` to `i32`, `u32` to `i64`, `f32` to `f64`, and an integer of 8, 16 or 32 bits to a float that holds it exactly (`u8`/`i8`/`u16`/`i16` to `f32`, any of them and `u32`/`i32` to `f64`). Everything else is written: narrowing, a sign change (`i32` to `u32`), `usize`/`isize` to or from a fixed width (32 bits on some targets, so `u64` to `usize` too), `i64`/`u64` to `f64`, a float to an integer. A literal keeps its rule: it takes the expected type if its value fits. `smith lint --fix` writes `.to(T)` where C converted implicitly and smith does not.
- An address is converted to a function type with `to`, the same rule as between a pointer and an integer (a pointer, or an integer as wide as one; a narrower integer is an error, #3110): what `dlsym` or a JIT gives, C's `((Run)address)()`. The result is a `fn` value like any other: called at once, stored or passed.
  ```smith
  Run := fn(i32) -> i32
  fn call_symbol(address: usize, n: i32) -> i32:
      (address.to(Run))(n)
  ```
- `to`, `wrap` and `cast` are built-in functions, so also method calls on any value. They are reserved as member names: no struct field is called `cast`, `to` or `wrap` (the tree's `Hooks.cast` is `cast_rule`, #2526); locals and parameters named `to` are fine. `as`, prefix `*p` and `(T)x` are errors whose fixes write these. There is no `&mut` and no `->`.

## Strings

- **Strings (D22, built by #2727 after the switch-over; until then a string has escapes but no holes):** `"..."` is the only string literal, with the usual escapes and holes: `console.println("x={x}")`, `"addr={format.hex(p)}"`. `{` opens a hole (`\{` writes a literal `{`; a `}` outside a hole is text, and `$` is not special). A string with holes is typed pieces, not a `str`: `format.string("id={id}")` makes one, from the current allocator. `"""` writes a string over several lines, dropping the newline after it and the lines' common indentation.

## Not settled yet
`[_]` lengths.
