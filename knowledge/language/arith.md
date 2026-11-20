---
type: specification
title: What arith proves
description: The procedure that proves a + - * fits and an index is in range, what facts it starts from, what it cannot decide, and what the compiler says when it cannot prove.
status: partial
built:
  - "`arith`, which decides linear arithmetic over the integers and proves that a plain `+`, `-`, or `*` fits, from the range of each value's type, the value of a literal, what a binding that is not a `var` was given, the condition of each `if` the operation is inside, joined with `and`, `or`, and `not`, and the range of each `loop i in a..b` over whole numbers, and, inside one round of a loop, what that round last gave each `var` of a whole number the loop assigns, stated against what it held when the round began, and what stays so after an `if` or a `match` some of whose blocks end in `break`"
  - "and the index of a `List` read as `xs[i]`, which `arith` proves below `xs.size`, and the `size` of a list as a value in a comparison, in a `requires`, an `if`, and the range of a `loop`, which a list has from the list it was given, whether bound to a name or to a `var`"
  - "and, for a `var` of a list, a struct, or a tuple, and each of its fields, what is known of it, as of any value, until it or a field of it is assigned, at which a fact about it is forgotten, in a loop, a lambda, and an arm of an `if` or a `match` as it is outside one, and a `var` the loop or lambda assigns is not known from before it"
  - "and the range of what `bit_and`, `bit_or`, `bit_xor`, `shift_right`, and `shift_left` give, of an unsigned number or, for a shift left, of any whole number, from what is known of the numbers they are given"
  - "and, for a `var` of a whole number, outside a loop as inside one, what it was last given, where no assignment to it lies between that and the use on any path, an `if`, a `match`, a loop that may run no time, an earlier round of a loop, and a lambda each counting as one that may lie between"
  - "and `/` and `%`, whose divisor `arith` must prove is not zero, and, for a signed number, that the least number is not divided by -1 where the result would not fit, and which it treats as one unknown quantity of the range of the result, or as the number itself when both sides are literals"
  - "and `+`, `-`, and `*` on a `Nat`, proven as on any whole number, with the fix \"check the operands first\", as a `Nat` has no wrapping, saturating, or checked function"
  - "not the facts of a `match` block, or a proof held in a value"
---

# What arith proves

`arith` is the procedure that proves, when the program is built, that a value fits the range it enters, as [Integer arithmetic](arithmetic.md) says, and that an index is in range. It decides linear arithmetic over the integers and nothing more, which is why it always answers yes or no, in a time that can be predicted.

```
use std.console (console, print)

fn main() ! console, alloc {
  a: Byte = 20
  b: Byte = 5
  print("{a * b}")
}
```

This prints `100`. `arith` knows that `a` is 20 and `b` is 5, so `a * b` is at most 255. [Integer arithmetic](arithmetic.md) shows what is refused when it cannot know.

## What it starts from

`arith` uses every fact in scope. Each of these is one.

**The range of a type.** A `u8` is at least 0 and at most 255, so `255 - x` never goes below 0.

```
fn rest(x: Byte) -> Byte { 255 - x }
```

`rest(250)` is `5`.

**The value of a literal, and of a name that is not a `var`.** A name is what it was given, so what is known of the value is known of the name.

```
fn bump(x: Byte) -> Byte {
  if x < 100 {
    y = x + 100
    y + 50
  } else {
    0
  }
}
```

`bump(7)` is `157` and `bump(200)` is `0`. In the block, `x < 100` gives `y = x + 100 < 200`, and so `y + 50 < 250` fits.

**The condition of an `if`.** The block it guards has it as a fact, and the `else` has its opposite. The condition may be joined with `and`, `or`, and `not`.

```
fn shift(x: Byte) -> Byte {
  if x < 10 and x > 3 { x + 200 } else if not (x < 100) { 0 } else { x + 100 }
}
```

`shift(5)` is `205` and `shift(50)` is `150`. In the first block `x` is below 10, and `x + 200` is below 210. In the last, `not (x < 100)` has failed, so `x` is below 100.

A block of a `match` is the same: what its pattern says is a fact in it.

```
fn before(x: Byte) -> Byte {
  match x {
    0 { 0 }
    n { n - 1 }
  }
}
```

`before(5)` is `4` and `before(0)` is `0`. In the second arm `x` is not `0`, so `n - 1` is not below 0.

**The range of a `loop`.** Inside `loop i in 0..n`, `i` is below `n`, as [Loops](loop.md) says. So `i + 1` fits where `i + 2` does not.

**The facts of a `var`.** A `var` has the facts its type states, and, where no loop assigns it, those of the value it was last given. Inside one round of a loop that assigns it, it also has those of the value that round last gave it, stated against what it held when the round began.

```
fn five() -> Byte {
  var v: Byte = 5
  v + 100
}
```

`v` is last given `5`, so `v + 100` is 105, which fits.

Inside a loop that assigns a `var`, what is known is what that round last gave it, stated against what it held when the round began, and a fact that holds when the round began holds still.

```
fn rounds() -> Byte {
  var v: Byte = 100
  var t: Byte = 0
  loop i in 0..3 {
    v = 50
    t = v + 200
  }
  t
}
```

`rounds()` is `250`. In the round, `v` was just given `50`, so `v + 200` fits. A fact is also kept after an `if` or a `match` some of whose blocks end in `break`: only the blocks that go on reach the line after it.

```
fn climb(x: Byte) -> Byte {
  var v: Byte = 0
  loop i in 0..10 {
    if x > 200 { break }
    v = x + 50
  }
  v
}
```

`climb(100)` is `150` and `climb(250)` is `0`. The line after the `if` is reached only when `x > 200` is false, so `x + 50` fits. Without the `if`, `v = x + 50` is refused, as `x + 50` may be above 255, which `v` does not hold.

**The size of a list.** A list has the `size` of the list it was given, whether it is bound to a name or to a `var`, and `size` is a value in a comparison, as in a `requires`, an `if`, and the range of a `loop`. It is what proves an index, as [Indexing](indexing.md) says.

```
use std.bits (u64)

fn plus(xs: List[Byte]) -> u64 {
  if xs.size < 100 { xs.size + 100 } else { 0 }
}

fn at(xs: List[Byte], i: u64) -> Byte {
  var ys = xs
  if i < ys.size { ys[i] } else { 0 }
}
```

`plus([1, 2, 3])` is `103`, and `at([1, 2, 3], 1)` is `2` while `at([1, 2, 3], 5)` is `0`. In `plus`, `xs.size < 100` makes `xs.size + 100` fit, and in `at`, the `ys` that `xs` was given has the size of `xs`, so `i < ys.size` proves `ys[i]`.

**A `var` of a list, a struct, or a tuple.** What is known of it, and of each of its fields, is known as of any value, until it or a field of it is assigned. Then a fact about it is forgotten.

```
struct Pair {
  a: Byte
  b: Byte
}

fn kept(q: Pair) -> Byte {
  var p = q
  if p.a < 100 { p.a + 100 } else { 0 }
}

fn forgotten(q: Pair) -> Byte {
  var p = q
  if p.a < 100 {
    p.a = 250
    p.a + 100
  } else { 0 }
}
```

`kept(Pair(a: 5, b: 1))` is `105`. `forgotten` is refused, as `p.a + 100` may be above 255, since `p.a` was assigned after `p.a < 100` was known. The same holds in a loop, a lambda, and an arm of an `if` or a `match` as outside one, and a `var` that the loop or lambda assigns is not known from before it.

**A bit operation.** What [`bit_and`, `bit_or`, `bit_xor`, `shift_right`, and `shift_left`](bits.md#a-number-that-is-not-negative) give for numbers that are not negative has the exact range that page says, from what is known of the numbers they are given.

```
use std.bits (bit_and, bit_or, shift_left, shift_right)

fn low(x: Byte) -> Byte { bit_and(x, 31) + 200 }

fn join(x: Byte, y: Byte) -> Byte {
  if x < 8 and y < 8 { bit_or(x, y) + 248 } else { 0 }
}

fn high(x: Byte) -> Byte { shift_right(x, 3) + 224 }

fn up(x: Byte) -> Byte { if x < 64 { shift_left(x, 2) + 3 } else { 0 } }
```

`low(255)` is `231`, `join(7, 1)` is `255`, `high(255)` is `255`, and `up(63)` is `255`. `bit_and(x, 31)` is at most 31, so `+ 200` fits. Both numbers of `bit_or` are below 8, so it is below 8. `shift_right(x, 3)` is at most 31. `shift_left(x, 2)` is `x * 4`, which is below 256 when `x < 64`.

It claims no more. `bit_and(x, 63) + 200` is refused, since 63 + 200 does not fit a `u8`, and so is `bit_or(x, y) + 248` with `x < 8` and `y < 9`, which is below 16. Nothing is known of what these give for a number that may be negative but the range of its size, and a shift by an amount that is not proven is refused, as [Bit operations](bits.md) says.

**A contract.** The function's own `requires`, and the `ensures` of a function that was called, are facts. [Contracts](contracts.md) says how.

**A proof held in a value.** Every proof it can see, as a value or as a field of a value, is a fact. [A struct that holds a proof](proof-fields.md) says how a value comes to hold one.

## What it cannot decide

`arith` decides comparisons between sums and differences of variables and constants, where any multiplication is by a constant, such as `i + k < n + w - 1` and `2 * n + 1 <= m`. Comparisons may be joined with `and`, `or`, and `not`.

```
fn left(n: Byte, m: Byte) -> Byte
  requires 2 * n + 1 <= m
{
  m - 2 * n
}
```

`left(3, 9)` is `3`. Here `2 * n` is multiplication by a constant, so `m - 2 * n` is proven to be at least 1.

It does not reason about a product of two variables, or about dividing by a variable. It treats such a term as one unknown quantity, and knows only the facts stated about that same term.

```
fn area(w: Byte, h: Byte) -> Byte { w * h }
```

This is refused, as `w * h` may be above 255, which the result does not hold. Nothing is known of `w * h` from what is known of `w` and `h`.

A fact stated about the same product is a fact about that term, so it is known.

```
fn area(w: Byte, h: Byte) -> Byte
  requires w * h <= 255
{
  w * h
}
```

`area(15, 17)` is `255`. So `rows * cols <= limit` is never proven from facts about `rows` and `cols` alone. A function that needs it states it in a `requires`, and a caller whose sizes are known only when the program runs gets it from a [`check`](check.md).

A caller that cannot show it is refused where it calls.

```
fn caller(x: Byte, y: Byte) -> Byte { area(x, y) }
```

This is refused: "cannot prove that `x * y <= 255`, which `area` requires of its arguments".

A `-` on a `Nat` compiles only when `arith` can show that the second operand is at most the first.

```
fn drop(n: Nat, k: Nat) -> Nat {
  if k <= n { n - k } else { 0 }
}
```

`drop(5, 2)` is `3` and `drop(2, 5)` is `0`. Without the `if`, `n - k` is refused, since `k <= n` is not known.

## When it cannot prove

When `arith` cannot prove a goal, the compiler reports the goal and the line that needs it.

```
fn double_plus(i: Byte, n: Byte) -> Byte
  requires i < n
{
  2 * i + 1
}
```

This is refused, as `2 * i` may be above 255, which the result does not hold, with the fix to check the operands first. Nothing says `n` is small, so `i` may be 254.

The author supplies the missing fact in one of three ways: with a [`requires`](contracts.md), with a [`check`](check.md), or with [a type that carries the proof](proof-fields.md). Adding `n <= 100` to the `requires` above makes the function compile.

## The forms

| Fact | Where it comes from |
| --- | --- |
| The limits of a type | The type of each value |
| What a literal or an unassigned name is | The binding |
| A condition | The `if` whose block the operation is in |
| `i` below the end | The range of `loop i in a..b` |
| What a `var` was last given | The value assigned, in a round of a loop or outside any |
| What a caller owes | The `requires` of the function |
| What a function gave | The `ensures` of a function that was called |
| What a bit operation gave | The numbers it was given |
| A proof | A value or field that holds one |

Next: [Contracts](contracts.md)
