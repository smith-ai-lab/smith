---
name: smith-dev-syntax-compile-time
description: "New syntax (#1428): `const` values and `const` parameters, compile-time `if`, reflection lists (`fields(T)[0]`, `countof(fields(T))`, unrolled `for f in fields(T):`, `parameters(F)`, `methods(T)`, `f.name`/`f.type`/`f.attr(X)`/`f.doc`/`f.default`, `value.[f]`), variadics (`rest...`, `nums: i32...`, `nums: int...`, spreading `rest...`), `static_assert`, `target`, and what replaced `constexpr`. Load when writing compile-time or reflective code in the new syntax."
---

# Compile time, reflection and variadics

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Compile time

```smith
const N = 64                                    # const demands compile-time evaluation

fn table(const n: usize) -> [n]f32:             # the argument must be known at compile time
    for i in range(0, n): math.sin(i.to(f32))

static_assert(N % 8 == 0)

fn page_size() -> usize:
    if target.os == .macos: 16384 else: 4096    # target is built in: no import
```

- `constexpr`, `if constexpr` and `for constexpr` are removed. A declaration whose value is known at compile time is a compile-time constant on its own; `const` demands it, on a declaration or a parameter.
- **Decided 2026-11-10 (D23, D24, D25 in `knowledge/decisions.md`): `const` is removed.** Write `N := 64` at module level (always computed by the compiler) and `fn table(n: usize) -> [n]f32` (a parameter is compile-time when a use needs it; otherwise the function works both ways). Any ordinary function runs at compile time where a constant is needed, with no mark; an expression of type `type` is allowed wherever a type is written; a compile-time value that crosses into the program is immutable there. The front end still accepts `const` until #3041 merges, and calls at compile time arrive with #3039: new tickets and examples use the decided forms.
- An `if` whose condition is known at compile time keeps one branch. `target.os` is the fact every declaration needs today; the others (`target.arch`, sanitizers) are not settled. `target.fast_math` is a `bool`: true when the compile has `-ffast-math` or `-ffinite-math-only` (D14); clang's `__FINITE_MATH_ONLY__` is written so.
- Built so far (#3039, step 1 of 3): one evaluator computes arithmetic, comparisons and `and`/`or`/`not` on constants, `x.to(T)`, `sizeof`/`alignof`/`countof`, types as values (`u8 != T`), concepts and `target`'s facts. It is asked for an array length (`[sizeof(Entry) * 8]u8`), an `if` condition in any body (`if sizeof(T) == 4:` keeps one branch per instance), `static_assert` (at module level and in a body), a `match` pattern and a `const`. Overflow, a division by zero and a value that does not fit are compile errors ("this overflows `I32` at compile time: 2147483647 + 1"); a value that is not known says why ("`n` is a variable that can change"). A `const` that holds a type is a name of it (`const Element = typeof(xs[0])`). A call of a function is not run yet (step 2), and structs, arrays and strings are not values yet (step 3).

## Reflection

```smith
fn show[T](value: *T):
    for f in fields(T):                         # a compile-time list: the loop is unrolled
        if num(f.type):                         # a compile-time condition keeps one branch
            stdio.printf("%.*s = %g\n", f.name, value.[f].to(f64))

first := fields(Point)[0]                       # indexed with a compile-time number
n := countof(fields(Point))
```

- `fields(T)` and `parameters(F)` are compile-time lists like packs. A run-time index or one out of range is a compile error. Each entry has `.name`, `.type`, `.attr(X)` (`?X`, [smith-dev-syntax-attributes](../smith-dev-syntax-attributes/SKILL.md)) and `.doc` (its `##` comment); `value.[f]` reads the field `f` names.
- `fields(T)` in another module lists only `pub` fields; it also reads a named tuple. Built so far (#2295): structs and `parameters(F)` of a plain function; `.attr(X)` is read in `if let .some(a) = f.attr(X): ... else: ...`, decided at compile time; `f.type` is the type, so `sizeof(f.type)` works; `f.default` is the field's default (a keyword is a member name after `.`), an error on a field with none: "`y` has no default" (#2543); a named tuple, `methods(T)` and the defaults of `parameters(F)` are not yet.
- `methods(T)` lists the functions in `T`'s module whose first parameter is `T`, `*T` or `*mut T`; Objective-C and C++ registration are built on it.

## Variadics

```smith
fn log(msg: str, rest...):                      # untyped: any types
    write(msg)
    for x in rest: show(&x)                     # unrolled
    for (i, x) in enumerate(rest): sep(i)       # unrolled, `i` a compile-time usize (D20)
fn total(nums: i32...) -> i32: ...              # typed: every argument is i32
fn widest(nums: int...) -> usize: ...           # a concept: each keeps its own type
fn trace(rest...): log("trace", rest...)        # forwarding spreads the pack
```

A pack is always compile-time: `countof(nums)`, `nums[i]` with a compile-time `i` (the `i` of `for (i, x) in enumerate(nums):` is one; owner, 2026-11-07: "A"), an unrolled `for`, and forwarding with `nums...`. It cannot hold a run-time slice; a run-time list is `nums: []i32`. Leftover named arguments are `; kw...` ([smith-dev-syntax-functions](../smith-dev-syntax-functions/SKILL.md)).
