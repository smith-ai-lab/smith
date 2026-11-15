---
type: specification
title: The Smith language
description: Every feature of Smith, introduced one at a time with examples. Anything not in this file is not in the language.
---

# The Smith language

Smith is a language written mainly by models, for three kinds of program: linear algebra with automatic differentiation on CPU and GPU, concurrent servers, and native applications. Its compiler is to be written in Smith itself.

This file builds the language up one concept at a time. Each section uses only what earlier sections introduced, and the examples grow into an MLP and then a transformer. The syntax is defined in [grammar.md](grammar.md), and how it is compiled is in [compiler.md](compiler.md). Nothing here has been implemented or measured.

A few rules hold throughout. Every body is a block in braces, statements are separated by newlines, and each program has one canonical formatting. A function's signature states everything a caller needs. Values are never shared by reference. Proofs are written in the source and only checked by the compiler.

The design follows four principles:

- Fewest features. Nothing speculative is included, and a feature is added only when real code needs it.
- The syntax is fixed and its meaning is extensible through traits, as with operators, literals, `loop`, and `try`.
- Effects are permissions. What a function may do is visible in its signature.
- No dependencies. Everything is written in Smith, with no other language in a Smith program.

## Functions

A function has typed parameters, a return type, and a block for a body. The last expression of a block is its value. A comment starts with `//` on its own line.

```
// The area of a rectangle.
fn area(w: f32, h: f32) -> f32 { w * h }

fn double(x: f32) -> f32 { x * 2.0 }
```

A call is `area(2.0, 3.0)`, and arguments may be named, as in `area(w: 2.0, h: 3.0)`.

## Bindings

`x = e` defines a new immutable name. A name is defined once per function, so there is no shadowing.

```
fn hypot(a: f32, b: f32) -> f32 {
  aa = a * a
  bb = b * b
  sqrt(aa + bb)
}
```

## Mutable locals and `if`

`var` defines a local that can change, and it may state its type, as in `var n: i32 = 0`. `:=` replaces its value, and `+=`, `-=`, `*=`, and `/=` accumulate into it. `if` is an expression, and the words `not`, `and`, and `or` combine conditions.

```
fn relu(x: f32) -> f32 {
  if x < 0.0 { 0.0 } else { x }
}

fn grow(start: f32, rate: f32) -> f32 {
  var total = start
  total := total * (1.0 + rate)
  total += 1.0
  total
}
```

## Structs and tuples

A struct holds named fields, one per line. Construction is a call with named fields, and `.` reads a field. A tuple groups values without names and is taken apart by a pattern, in which `var` can mark a single binding. A definition may also sit at the top level of a file, as `unit` does below.

```
struct Rect {
  w: f32
  h: f32
}

fn rect_area(r: Rect) -> f32 { r.w * r.h }

fn sides(r: Rect) -> (f32, f32) { (r.w, r.h) }

fn perimeter(r: Rect) -> f32 {
  (a, var b) = sides(r)
  b := b * 2.0
  a * 2.0 + b
}

unit = Rect(w: 1.0, h: 1.0)
```

## Enums and `match`

An enum holds exactly one of its cases. Each case names a type, written `name: Type` like a struct field, or is bare. `match` takes a value apart, with a block per case. A case is always written with a leading dot, in a pattern and in an expression alike, and a bare name in a pattern always introduces a new variable.

```
struct Circle {
  radius: f32
}

enum Shape {
  circle: Circle
  rect: Rect
  empty
}

fn shape_area(s: Shape) -> f32 {
  match s {
    .circle(c) { 3.14159 * c.radius * c.radius }
    .rect(r) { rect_area(r) }
    .empty { 0.0 }
  }
}

fn unit_square() -> Shape { .rect(Rect(w: 1.0, h: 1.0)) }
```

A case is built with a leading dot, as in `.rect(...)` or `.empty`, wherever the expected type is known. Otherwise it is written in full, as in `Shape.rect(...)`. Because a case names a type, the `Rect` inside a `Shape` can be passed on its own after a match.

## Generics and traits

Type parameters go in brackets after a name. A trait is a set of functions over one or more types, and an `impl` supplies them for particular types. A trait parameter marked `out` is determined by the others: once the two operand types of `add` are known, its result type is fixed. A bound such as `T: Add` requires an implementation in which every parameter is `T`.

```
enum Option[T] {
  some: T
  none
}

trait Add[A, B, out C] {
  fn add(a: A, b: B) -> C
}

impl Add[Rect, Rect, Rect] {
  fn add(a: Rect, b: Rect) -> Rect { Rect(w: a.w + b.w, h: a.h + b.h) }
}

fn twice[T: Add](x: T) -> T { x + x }

big = twice(unit)
both = add(unit, unit)
```

- A trait function is called like any other function, as in `add(unit, unit)`. It is qualified, as in `Add.add`, only when two names clash.
- Operators are trait functions: `a + b` means `Add.add(a, b)`. The arithmetic operators are `+`, `-`, `*`, `/`, and `^` for a power.
- A trait has at most one implementation for each choice of its types that are not `out`, and calls are specialized at compile time, so traits cost nothing at run time.

  The operands of an operator may differ in type, so a matrix times a vector, or a scalar times a struct of weights, is one more `impl` of `Mul`.
- `derive Add for Circle` generates an implementation field by field.

## Literals and numbers

Every literal form is backed by a trait, and the expected type picks which implementation runs. That is why `2.0` above became an `f32`.

| Literal | Trait | Can become |
| --- | --- | --- |
| `5` | `FromInt[T]` | `i32`, `u8`, `f32` |
| `2.5` | `FromFloat[T]` | `f16`, `bf16`, `f32`, `f64` |
| `"hi"` | `FromString[T]` | `str` |
| `[1, 2, 3]` | `FromArray[T]` | An array of three elements |

- Conversion runs at compile time, so `300` as a `u8` is a compile error.
- A literal with nothing to determine its type is an error.
- Only literals are flexible. A value of one numeric type becomes another only through an explicit call such as `float(n)`.
- The numeric types are `i8` to `i64`, `u8` to `u64`, `f16`, `bf16`, `f32`, and `f64`, plus `bool`. A `str` is bytes known to be valid UTF-8.

  Integers also have bitwise operations and shifts, as named functions.

Integer arithmetic never overflows silently. A plain `+`, `-`, or `*` compiles only when the result provably fits, and no check is ever inserted behind the author's back. When the compiler cannot prove it, the author picks a behaviour by name. The proof machinery appears under [Contracts and proofs](#contracts-and-proofs).

```
// Does not compile: i may be 255.
fn next(i: u8) -> u8 { i + 1 }

// 255 becomes 0.
fn next_wrapping(i: u8) -> u8 { wrapping_add(i, 1) }

// 255 stays 255.
fn next_saturating(i: u8) -> u8 { saturating_add(i, 1) }

// 255 gives .none.
fn next_checked(i: u8) -> Option[u8] { checked_add(i, 1) }
```

Most integer arithmetic is on indices and sizes, whose limits are already in their types, so the proof is usually automatic.

## Lambdas, `with`, and `try`

A lambda is `fn(x) { e }`, and a function type is written `(T) -> U`.

When the last argument of a call is a lambda with no parameters, it may be written as a block after the call: `f(a) { e }`, or `f { e }` when there are no other arguments. The block must start on the same line as the call.

```
fn unless(skip: bool, otherwise: f32, body: () -> f32) -> f32 {
  if skip { otherwise } else { body() }
}

fn fee(is_member: bool, w: f32, h: f32) -> f32 {
  unless(is_member, 0.0) { area(w, h) * 0.1 }
}
```

The body of `fee` means `unless(is_member, 0.0, fn() { area(w, h) * 0.1 })`. The formatter always uses the trailing form.

`with x = f(args)` passes the rest of the enclosing block to `f` as a lambda taking `x`, added as the last argument. It is sugar for a call, and it keeps code flat.

```
enum Result[T, E] {
  ok: T
  err: E
}

fn then[T, U, E](r: Result[T, E], body: (T) -> Result[U, E]) -> Result[U, E] {
  match r {
    .ok(x) { body(x) }
    .err(e) { .err(e) }
  }
}

fn parse_pair(a: str, b: str) -> Result[(i32, i32), ParseError] {
  with x = then(parse_int(a))
  with y = then(parse_int(b))
  .ok((x, y))
}
```

The body of `parse_pair` means exactly this:

```
then(parse_int(a), fn(x) {
  then(parse_int(b), fn(y) {
    .ok((x, y))
  })
})
```

The binding is optional: `with f(args)` passes a lambda that takes no parameters. The lambda is inlined and cannot escape, so `with` costs nothing at run time.

Continuing only on success is common enough to have a keyword. `x = try e` means `with x = then(e)`, so `parse_pair` is written like this:

```
fn parse_pair(a: str, b: str) -> Result[(i32, i32), ParseError] {
  x = try parse_int(a)
  y = try parse_int(b)
  .ok((x, y))
}
```

An error is an ordinary value. If `parse_int(a)` fails, `parse_pair` returns that failure and the remaining lines do not run. Nothing unwinds: `try` only returns a value, so code after a call always runs. `try` also works inside a larger expression, as in `.ok((try parse_int(a), try parse_int(b)))`, and as a statement when there is no value to bind. The formatter always writes `try e`.

The keyword is fixed and its meaning is extensible. `then` is the one function of the `Try` trait, which relates a container `C`, the final result `R`, and the value inside the container, `T`, and `try` works on any type that has an implementation. The function above is the one for `Result`. An `impl` takes its own type parameters in brackets, and this is the one for `Option`:

```
trait Try[C, R, out T] {
  fn then(c: C, body: (T) -> R) -> R
}

impl[T, U] Try[Option[T], Option[U], T] {
  fn then(c: Option[T], body: (T) -> Option[U]) -> Option[U] {
    match c {
      .some(x) { body(x) }
      .none { .none }
    }
  }
}
```

Any type joins by adding an `impl`. Using `try` on an `Option` inside a function that returns a `Result` does not compile, because no implementation relates the two, so the conversion is written out.

## Effects

A function with no `!` clause is pure. Effects are listed after the return type, and a caller must have every effect its callees use. A function without an effect cannot perform it, so effects work as permissions. Below, `load` is shown by its signature alone.

```
fn load(path: str) -> Result[Bytes, IoError] ! fs, alloc

fn fixture_config() -> Result[Config, IoError] ! alloc {
  with memory_fs(fixtures)
  bytes = try load("model.toml")
  parse_config(bytes)
}
```

- The built-in effects are `fs`, `net`, `time`, `ui`, `gpu`, `foreign`, `alloc`, `spawn`, `exec` for running another program, and `diverge` for code that may not terminate.
- A handler is a function that runs a lambda with an effect handled, installed with `with`. It is declared by putting the effect on its lambda parameter, as in `fn memory_fs[T](files: Files, body: () -> T ! fs) -> T`. `memory_fs` above handles `fs` with an in-memory file system, so `fixture_config` does not carry `fs`.
- The stored signature always states the effect list. The compiler proposes it while the function is being written, and lambdas are inferred.
- A lambda's other effects are checked where it is written: whatever its body does must be allowed by the function it sits in, and the function receiving it says nothing about them. This is sound because a lambda passed to a call cannot be stored or returned. There are no effect variables.
- A function stored in a value states its effects in its type, and is pure when none are written.

## Ownership

Two traits say what may happen to a value. `Copy` means it may be duplicated, and `Discard` means it may be dropped unused. Both are derived automatically: a type has each trait unless one of its fields lacks it.

| Kind | Traits | Examples |
| --- | --- | --- |
| Ordinary | `Copy` and `Discard` | Numbers, arrays, most structs |
| Used at most once | `Discard` only | A random `Key` |
| Used exactly once | Neither | A raw file handle, a transaction |

Passing an argument lends it: the callee cannot keep, store, or return it. A parameter marked `take` receives ownership, and the caller cannot use the value again. For `Copy` types the difference is invisible.

```
fn split(k: take Key) -> (Key, Key)
fn uniform(k: take Key) -> f32

fn two_numbers(key: take Key) -> (f32, f32) {
  (k1, k2) = split(key)
  (uniform(k1), uniform(k2))
}
```

Using `k1` twice would be a compile error, so a random key cannot be reused. Randomness needs no effect, because every random function takes a key.

A type opts out of a trait it would get automatically with a `without` statement, which works for any automatically derived trait. Its fields stay plain.

```
struct Transaction {
  id: u64
}

without Copy for Transaction
without Discard for Transaction

fn commit(t: take Transaction) -> Result[(), DbError] ! net

fn transfer(db: Db) -> Result[(), DbError] ! net {
  t = begin(db)
  debit(t, 100)
  commit(t)
}
```

`debit` borrows `t` and `commit` takes it. Leaving out `commit(t)`, or calling it twice, does not compile.

A value with neither trait must be taken exactly once on every path. That is how a resource wrapper is checked. `open` below is written for `with`: it lends the handle to the rest of the caller's block and closes it afterwards. Leaving out `os_close`, or putting a step that can fail between opening and closing, does not compile. The body's own effects, here `alloc` from `read`, are allowed because `first_bytes` declares them.

```
fn open[T](path: str, body: (File) -> Result[T, IoError]) -> Result[T, IoError] ! fs {
  handle = try os_open(path)
  r = body(handle)
  os_close(handle)
  r
}

fn first_bytes(path: str) -> Result[Bytes, IoError] ! fs, alloc {
  with f = open(path)
  read(f, 16)
}
```

Most code only ever writes the `with` line. Generic functions state `T: Copy` where they copy, and tooling proposes the bound.

## Arrays and sizes

An array's size is part of its type: `[3]f32` is three floats and `[m, k]f32` is an `m` by `k` matrix. Size parameters are written in brackets with type `Nat`. An index into an axis of size `n` has type `Fin[n]`, a number known to be below `n`, so indexing needs no bounds check.

```
fn first_of_three(a: [3]f32) -> f32 { a[0] }

fn entry[m: Nat, k: Nat](a: [m, k]f32, r: Fin[m], c: Fin[k]) -> f32 {
  a[r, c]
}
```

- A literal index is checked against the size at compile time, so `a[3]` on a `[3]f32` is a compile error.
- A size known only at run time is written `[some n]T`.
- A range is `a..b` and excludes `b`. `x[a..b]` has size `b - a`, with `x[..k]` and `x[k..]` as short forms.

## `for` expressions

`for i { e }` describes an array: the value `e` at every index `i`. The size comes from how `i` is used, and several indices give several axes. A `for` is a view, a rule for computing each element, so it allocates nothing by being written.

```
trait MatMul[A, B, out C] {
  fn matmul(a: A, b: B) -> C
}

// vector @ vector
impl[n: Nat] MatMul[[n]f32, [n]f32, f32] {
  fn matmul(a: [n]f32, b: [n]f32) -> f32 {
    sum(for i { a[i] * b[i] })
  }
}

// matrix @ vector
impl[m: Nat, n: Nat] MatMul[[m, n]f32, [n]f32, [m]f32] {
  fn matmul(w: [m, n]f32, x: [n]f32) -> [m]f32 {
    for r { sum(for c { w[r, c] * x[c] }) }
  }
}

// matrix @ matrix
impl[m: Nat, k: Nat, n: Nat] MatMul[[m, k]f32, [k, n]f32, [m, n]f32] {
  fn matmul(a: [m, k]f32, b: [k, n]f32) -> [m, n]f32 {
    for i, j { sum(for l { a[i, l] * b[l, j] }) }
  }
}
```

- `sum`, `max`, and `mean` reduce a view to a value, and `map(f, v)` is shorthand for `for i { f(v[i]) }`.
- Arithmetic operators on arrays are element-wise and return views, so `x + y` is `for i { x[i] + y[i] }`. The one product that is not element-wise is `@`: `a @ b` means `matmul(a, b)`, which contracts the last axis of `a` with the first axis of `b`, and it binds like `*`. The sizes are checked by the types. `@` does not treat leading axes as batches, so a batch is written as a `for`.
- Iterations are independent, so every `for` may run across SIMD lanes, threads, or a GPU.
- A `for` whose block only accumulates into a `var` yields nothing. Accumulation with `+=` or `*=` is the one mutation allowed inside it, and it runs in parallel as a reduction. Replacing an outer variable with `:=` is rejected, because the result would depend on order.
- Float code is fast by default and repeatable: the same code, built by the same compiler, on the same computer, always gives the same bits, whatever the thread count. Different hardware or a newer compiler may change the last bits.
- An accumulation may be indexed, as in `hist[bucket[i]] += 1`. This is how a scatter is written, and it is also the gradient of an indexed read.
- A float reduction is regrouped for speed, in a shape that depends only on the source and the target. An outermost `sum` is split into several running totals that are combined in pairs. A `sum` that produces one element of an array adds its terms in order, because the parallelism comes from the outer indices.
- `sum_ordered(x)` adds strictly left to right, for when exactness matters more than speed.
- A product whose only use is one add or subtract is fused with it into a single instruction with one rounding, so `a * b + c` always fuses, whether or not the product is named.
- No other float rewrite is allowed unless it is exact.

These are enough for the first piece of a neural network. A linear layer is a struct with shape-typed fields and one line of arithmetic.

```
struct Linear[i: Nat, o: Nat] {
  w: [o, i]f32
  b: [o]f32
}

fn apply[i: Nat, o: Nat](l: Linear[i, o], x: [i]f32) -> [o]f32 {
  l.w @ x + l.b
}
```

Passing the output of a `Linear[784, 256]` to a `Linear[128, 10]` is a compile error, because `256` and `128` do not match.

Random arrays follow the key rule from [Ownership](#ownership). A random function fills a whole array from one key, and the expected type gives the shape, as it does for a literal. When each iteration needs its own key, `split_n` turns one key into an array of keys.

```
fn init_linear[i: Nat, o: Nat](key: take Key) -> Linear[i, o] {
  (kw, kb) = split(key)
  Linear(w: normal(kw), b: uniform(kb))
}

fn dropout[n: Nat](x: [n]f32, rate: f32, key: take Key) -> [n]f32 {
  keys = split_n(key)
  for i {
    if uniform(keys[i]) < rate { 0.0 } else { x[i] }
  }
}
```

Reading `keys[i]` uses up that key. It is allowed inside a `for` when the body takes `keys[i]` at most once and `i` is the loop's own index, so every iteration touches a different key. Key `i` is computed directly from the parent key and `i`, so the array of keys never exists in memory and the loop still runs in parallel.

## `loop` and iteration

The keyword states whether order matters: a `for` has no order, and a `loop` runs in order, one step at a time. A `loop` runs over a range, an array, or any type that implements the `Iterate` trait below. An open-ended `loop { ... }` runs until `break`, and its function carries the `diverge` effect unless it states a measure that decreases. There is no `while`.

```
fn smooth[n: Nat](x: [n]f32) -> f32 {
  var acc = 0.0
  loop i in 0..n { acc := acc * 0.9 + x[i] }
  acc
}
```

A type becomes loopable by implementing `Iterate`. Its one function, `each`, walks the value and calls `body` on every element. `loop v in x { ... }` means `each(x, fn(v) { ... })`.

```
trait Iterate[C, out T] {
  fn each(c: C, body: (T) -> ())
}

struct Countdown {
  from: i32
}

impl Iterate[Countdown, i32] {
  fn each(c: Countdown, body: (i32) -> ()) {
    loop i in 0..c.from { body(c.from - i) }
  }
}

fn largest(c: Countdown) -> i32 {
  var top = 0
  loop v in c { top := max(top, v) }
  top
}
```

- The body is a lambda that cannot escape, so it is inlined and a `loop` over a user type costs nothing extra. Its effects are checked where it is written.
- `each` is an ordinary function, so `with v = each(x)` also works when the walk is the last thing a function does.
- A walk over a tree passes `body` along to its recursive calls.
- `break` is allowed in a loop over a range or an array and in an open-ended loop. It is not allowed in a loop over a user type, because the body has no way to tell `each` to stop.
- A range `a..b` is a `Range` value, and going backward is a reversed range, `loop i in reversed(0..n)`.

## Contracts and proofs

A signature can state what must hold. `requires` is owed by the caller and `ensures` by the function, about a result the signature names. Both are checked at compile time.

```
fn average[n: Nat](x: [n]f32) -> f32
  requires n > 0
{
  sum(x) / float(n)
}

fn clamp(x: f32, lo: f32, hi: f32) -> r: f32
  requires lo <= hi
  ensures lo <= r and r <= hi
{
  max(lo, min(x, hi))
}
```

Contracts are discharged by `arith`, a small deterministic procedure for arithmetic over sizes and indices. The compiler only checks. It never searches for a proof.

| Keyword | Who owes the proof | About |
| --- | --- | --- |
| `requires` | The caller | The arguments |
| `ensures` | The function | The result, by the name the signature gives it |
| `check` | Nobody: it tests at run time | Outside data. Yields a proof if the test passes |

`Fin[n]` from [Arrays and sizes](#arrays-and-sizes) is a number paired with a proof that it is below `n`. `check` is how outside data becomes one.

```
fn token[v: Nat](raw: Nat) -> Option[Fin[v]] {
  match check(raw < v) {
    .some(p) { .some(Fin(value: raw, below: p)) }
    .none { .none }
  }
}
```

There is no `panic`. A `check` that fails returns `.none`, and a failure that can really happen is returned as a `Result`. A function with neither the `alloc` nor the `diverge` effect therefore returns a value for every input its `requires` lines allow.

- A proof comes from `arith` or from a `check` that passed, and it is erased before code generation.
- Predicates used in contracts are ordinary functions returning `bool`, so a contract can be run on real data.
- Every function terminates unless it carries `diverge`, which means it may not finish. Structural recursion is accepted automatically, and anything else states a decreasing measure. Callers inherit `diverge`, and it is not allowed in a contract or a GPU kernel.
- `hole(name)` stands in for code or a proof not yet written. The compiler reports the type and the goal it needs.

Facts that recur belong in a type, so they are proved once and travel with the value. `Pos` is a `Nat` proven above zero and can be used wherever a `Nat` is expected, so a size declared `[n: Pos]` needs no `requires n > 0`. A struct can hold a proof as a field in the same way. Tooling proposes the remaining `requires` lines, as it proposes effects.

```
fn average[n: Pos](x: [n]f32) -> f32 { sum(x) / float(n) }
```

Index arithmetic is allowed wherever the sizes make it provable, so no index is ever checked at run time. A convolution states its input's size in terms of its output's, and `arith` proves that `i + k` is in range.

```
fn conv[n: Nat, w: Pos](x: [n + w - 1]f32, kernel: [w]f32) -> [n]f32 {
  for i { sum(for k { x[i + k] * kernel[k] }) }
}

fn conv2d[h: Nat, w: Nat, kh: Pos, kw: Pos](
  img: [h + kh - 1, w + kw - 1]f32,
  kernel: [kh, kw]f32,
) -> [h, w]f32 {
  for y, x { sum(for i, j { img[y + i, x + j] * kernel[i, j] }) }
}
```

Everyday code uses helpers that carry these proofs. `pad(x, 1, 1, 0.0)` adds a border, and `windows(x, 3)` gives every window of length 3, with an optional `stride`. Both are views that fuse into the loop that reads them. `for` is the primitive, and most users work with the layer above it: element-wise operators, `@`, reductions, and helpers such as these.

## Storage and arenas

Array code never asks for memory. The compiler decides which arrays need a buffer, by a fixed rule about how each one is read.

- **An array fuses into its consumer** when the consumer reads each element once. No buffer exists.
- **An array is stored** when its elements are read repeatedly, or when more than one consumer uses it.

```
fn squares_plus_five[n: Nat](xs: [n]f32) -> f32 {
  ys = for i { xs[i] * xs[i] }
  zs = for i { ys[i] + 5.0 }
  sum(zs)
}
```

This compiles to a single loop with no buffers, because `zs` reads each `ys[i]` once and `sum` reads each `zs[i]` once.

The MLP shows the other case. Every output of the second layer reads every hidden value, so `hidden` is stored, and the `relu` fuses into the loop that fills it.

```
struct Mlp[i: Nat, h: Nat, o: Nat] {
  l1: Linear[i, h]
  l2: Linear[h, o]
}

fn forward[i: Nat, h: Nat, o: Nat](m: Mlp[i, h, o], x: [i]f32) -> [o]f32 {
  hidden = map(relu, apply(m.l1, x))
  apply(m.l2, hidden)
}
```

Data whose size is known only at run time, such as a growing list or a map, allocates through the `alloc` effect. A function's temporaries are released when it returns, and its result is built directly in its caller's memory. `with arena()` handles `alloc` for the rest of a block with a fresh arena, which is freed whole when the block ends.

```
fn squares(n: Nat) -> List[f32] ! alloc {
  var out: List[f32] = []
  loop i in 0..n { out := push(out, float(i) * float(i)) }
  out
}

fn sum_of_squares(n: Nat) -> f32 ! alloc {
  sum(squares(n))
}

fn handle(req: Request) -> Response {
  with arena()
  respond(req, sum_of_squares(req.n))
}
```

- **Every dynamic value owns its storage.** There are no references, so no pointer can cross from one arena into another.
- **Every value's arena is known at compile time.** Nothing stores an arena pointer.
- **Results are built in place** in the caller's memory, so nothing is copied on return.
- A graph, or anything else with shared or cyclic links, is a collection plus indices into it, because a value cannot point back at itself.
- **A `var` that is replaced is updated in place** when its old value is provably unused.

Recursion cannot overflow the stack.

```
// A tail call: compiled as a loop, with no frame per call.
fn power(x: f64, n: Nat, acc: f64) -> f64 {
  if n == 0 { acc } else { power(x, n - 1, acc * x) }
}

// Not a tail call: one frame per level, kept in the arena.
fn power_slow(x: f64, n: Nat) -> f64 ! alloc {
  if n == 0 { 1.0 } else { x * power_slow(x, n - 1) }
}
```

- A tail call always compiles to a loop, so a tail-recursive function uses no stack and may run in a GPU kernel.
- Any other recursion keeps its frames in the current arena, so its depth is limited only by memory. Such a function carries `alloc`.
- Code with no recursion has a deepest chain of calls that is known at compile time, and the compiler confirms it fits the stack.

A type may contain itself, which is how a tree is written. The compiler stores such a field out of line, in the same arena as its parent, and no pointer appears in the source.

```
enum Expr {
  num: f64
  neg: Expr
  add: Pair
}

struct Pair {
  left: Expr
  right: Expr
}

fn eval(e: Expr) -> f64 ! alloc {
  match e {
    .num(v) { v }
    .neg(x) { -eval(x) }
    .add(p) { eval(p.left) + eval(p.right) }
  }
}
```

- A tree is a value. Values are immutable, so two trees may share a subtree and nothing can observe it, and a tree can never contain a cycle.
- Nodes are bumped into the arena and dropped with it, so there is no per-node allocation, reference count, or collector.
- No address is observable, so the compiler picks the layout, such as 32-bit offsets within the arena.
- Changing a tree builds new nodes and leaves the old ones until the arena is dropped. A pass that rewrites a tree many times runs in its own arena and builds its result in the parent's.

## Differentiation

`grad(f)` is a compile-time call that returns a new function: the gradient of `f` with respect to its first parameter. The gradient has the same type as that parameter. To differentiate with respect to something else, wrap `f` in a lambda that takes it first, and to differentiate with respect to several values, put them in one struct or tuple. `derive Vector` gives a struct of float arrays element-wise arithmetic and scaling, which is what a gradient step needs.

```
derive Vector for Linear
derive Vector for Mlp

fn mse[n: Pos](pred: [n]f32, target: [n]f32) -> f32 {
  sum(for k { (pred[k] - target[k]) ^ 2 }) / float(n)
}

fn loss[b: Pos, i: Nat, h: Nat, o: Pos](m: Mlp[i, h, o], xs: [b, i]f32, ys: [b, o]f32) -> f32 {
  mean(for n { mse(forward(m, xs[n]), ys[n]) })
}

loss_grad = grad(loss)

fn sgd_step[b: Pos, i: Nat, h: Nat, o: Pos](
  m: Mlp[i, h, o],
  xs: [b, i]f32,
  ys: [b, o]f32,
  lr: f32,
) -> Mlp[i, h, o] {
  g = loss_grad(m, xs, ys)
  m - lr * g
}

fn train[n: Nat](data: Dataset[n, 784, 10], steps: Nat, key: take Key) -> Mlp[784, 256, 10]
  requires n >= 64
{
  var m = init_mlp(key)
  loop step in 0..steps {
    (xs, ys) = batch(data, step, 64)
    m := sgd_step(m, xs, ys, 0.01)
  }
  m
}
```

`Dataset`, `batch`, and `init_mlp` are library pieces not shown here.

- `grad` works on the typed core form of `f`, where every `for` is still visible. Each primitive supplies its derivative rule through a `Differentiable` trait.
- A function can supply its own backward pass through that trait, with the obligation that it equals the derivative.
- `train` is a pure function of the data, the step count, and the key, so its result can be reproduced.
- The weights in `train` are updated in place, because the old `m` is never read after each step.

## Devices

The same source runs on a CPU or a GPU. `with device(gpu)` compiles the rest of the block to GPU kernels.

```
fn train_on_gpu[n: Nat](
  data: Dataset[n, 784, 10],
  steps: Nat,
  key: take Key,
) -> Mlp[784, 256, 10] ! gpu
  requires n >= 64
{
  with device(gpu)
  train(data, steps, key)
}
```

- Code inside the scope must have no `alloc` or I/O effects, no recursion, and only fixed-size data. Anything else is a compile error that names the reason.
- On Apple silicon the CPU and GPU share memory, so entering the scope copies nothing. Elsewhere, values are moved to the device on entry and results are moved back on exit.

## Tasks and async

Concurrent work runs in tasks, and there are no raw threads or locks. Data is immutable or uniquely owned, so data races cannot be written. `with tasks()` opens a task group. `spawn` takes the child's work as a trailing block (see [Lambdas, `with`, and `try`](#lambdas-with-and-try)) and starts it in the nearest group, and `join` waits for its result. Every child finishes before the block ends. A function that spawns without opening its own group carries the `spawn` effect.

```
fn fetch_both(a: str, b: str) -> Result[(Bytes, Bytes), NetError] ! net, alloc {
  with tasks()
  ta = spawn { fetch(a) }
  tb = spawn { fetch(b) }
  pa = try join(ta)
  pb = try join(tb)
  .ok((pa, pb))
}
```

A spawned block runs at the same time as its parent, so three rules say what it may use.

- An ordinary value is shared. It is immutable, and the child finishes before the group's block ends, so nothing is copied.
- A `var` is read as its value at the moment of the spawn and cannot be assigned. Results come back through `join`.
- A single-use value moves into the child, and the parent cannot use it afterwards.

```
fn serve(listener: Listener) ! net, alloc, diverge {
  with tasks()
  loop {
    conn = accept(listener)
    spawn { handle(conn) }
  }
}
```

Here `conn` belongs to the child after the spawn. A child runs under the handlers in force where it was spawned, except that it gets its own arena.

All I/O is async and reads as sequential code. A call such as `fetch(a)` carries the `net` effect. Under an async handler it suspends its task, and under a blocking handler it blocks, with the same source either way.

- A function with a suspending effect compiles to a state machine whose frame lives in its task's arena. Tasks need no stacks of their own, and pure code compiles as ordinary functions.
- Each child task gets its own arena, and its result is built in the parent's.
- A server is one task per connection with one arena per request.
- A value lives as long as the task that created it, and tasks pass plain data to each other.

Running out of memory ends the task that ran out, and nothing else. Its arena is dropped whole, the `fs` and `net` handlers close the handles it left open, and its parent sees the failure as an error from `join`. No other task shares its memory, so none is left in a broken state. Only the `alloc` handler can end a task, and user code has no way to ask for it. `main` is the root task, so running out of memory there ends the program.

Events are handled the same way. A native application is a task that waits for the next event, so user code has no callbacks, and the window lives exactly as long as `main`.

```
fn main() ! ui, diverge {
  with app = application()
  with win = window(app, "Smith", 800, 600)
  loop {
    e = next_event(win)
    match e {
      .close { break }
      .key(k) { handle_key(k) }
      .frame(t) { draw(win, t) }
    }
  }
}
```

## Foreign calls and native applications

A foreign declaration states a C signature by hand and carries the `foreign` effect. Declarations are trusted, so they live in a few audited modules.

```
foreign "libSystem" fn write(fd: i32, buf: Ptr[u8], len: usize) -> isize ! foreign
```

`Ptr[T]`, `usize`, and `isize` exist for foreign signatures and are not used in ordinary code.

System frameworks are reached the same way, with no C or Objective-C source in a Smith program.

- On macOS and iOS, a Smith library wraps AppKit and UIKit by calling the Objective-C runtime's C functions. Windows, menus, and native controls are ordinary Smith values behind `with`.
- The OS still calls in for a few things, such as a window procedure or a delegate method. The platform library exports a small Smith function with the C calling convention that pushes those onto an event queue, which is what `next_event` reads.
- The `ui` handler runs its tasks on the main thread.
- Rendering goes to a GPU layer in the window, filled each frame by Smith's own kernels.

## Modules

One file is one module, named by its path, so `db/transaction.smith` is the module `db.transaction`. Everything in a module is private unless marked `pub`, on an item or on a single field.

```
pub struct Rect {
  pub w: f32
  pub h: f32
}

pub struct Transaction {
  id: u64
}

pub fn begin(db: Db) -> Transaction ! net
pub fn commit(t: take Transaction) -> Result[(), DbError] ! net

fn next_id(db: Db) -> u64 ! net
```

- A struct can be built or taken apart only by code that can see all of its fields. Any module can build a `Rect`. Other modules can hold a `Transaction` and pass it on, but cannot forge one or dispose of one, which is what makes `without` and proof fields trustworthy.
- A private definition can be used only inside its own module.

Another module's names are brought in by an explicit list. There is no way to import everything.

```
use db.transaction (Transaction, begin, commit)

fn transfer(db: Db) -> Result[(), DbError] ! net {
  t = begin(db)
  commit(t)
}
```

- A small prelude is in scope everywhere: `Option`, `Result`, the `Try` trait, the numeric types, the operator traits, and the array functions such as `sum` and `map`.
- When two modules export the same name, the modules are imported and the name is qualified, as in `transaction.begin(db)`.
- The formatter sorts the list. A missing import is an error that names the module to import from, and an unused one is a warning from `smith check`.
- An `impl` is not imported. It is visible everywhere, and it must live in the module that defines the trait or the type, which keeps one implementation per type checkable.

A `use` with no list brings in the module itself, under its last path segment, and `as` gives it another name when two last segments clash. All `use` lines sit at the top of the file, one per module, and nothing is re-exported.

```
use db.transaction
use ui.animation as anim

t = transaction.begin(db)
a = anim.begin(view)
```

## Projects and packages

The standard library is large, so that outside code is rarely needed. It covers arrays, differentiation, and neural network layers; tasks, networking, and an HTTP server; serialization through `derive`; files, time, random numbers, text, hashing, and compression; and the platform layer for windows and events.

Outside code is vendored: it is copied into the project as ordinary modules and imported like any other, as in `use vendor.toml.parse (parse)`. There is no registry and there are no version ranges. One file at the project root, `project.smith`, records where each vendored folder came from and its content hash.

```
use std.project (Generate, Package, Vendored)

package = Package(
  name: "site",
  entry: "main",
  vendored: [
    Vendored(path: "vendor/toml", source: "https://example.com/toml", hash: "b3:9f2c"),
    Vendored(path: "vendor/sqlite", source: "https://example.com/sqlite", hash: "b3:41ae"),
  ],
  generate: [
    Generate(run: route_table, inputs: "routes/", output: "generated/routes.smith"),
  ],
)

fn route_table(dir: Dir) -> Source ! fs, alloc {
  render_router(files(dir))
}

pub fn deploy(env: str) -> Result[(), DeployError] ! fs, net, alloc {
  binary = try build(package, target: .linux_x64)
  upload(binary, to: env)
}
```

- Every dependency is listed by the project, including dependencies of dependencies. The build checks each folder against its hash and never touches the network.
- A vendored function's effects are in its signature like any other, and a caller must declare them to call it. A parser with no effects is pure, whatever it contains.
- Nothing runs because a dependency is present. Its code runs only when the project calls it.
- A build step is a function listed under `generate`. The build runs it before compiling, on every build, so generated source cannot go stale. A step may read the project's files and nothing else, with no network and no clock. Its output is an ordinary source file, which is how file-based routing, embedded assets, and types generated from a schema file are done.
- A task is a public function in `project.smith`, run by name, as in `smith deploy --env staging`. Its parameters are its command-line flags and its effects say what it may do. Running another program needs the `exec` effect.
- There are no macros that rewrite code in place.

A module's name is its path from the project root, and the standard library lives under `std`. Every field of `Package` is always written, since there are no default field values.

```
serving/
  project.smith
  main.smith
  model/
    gpt.smith
    gpt_test.smith
  routes/
    health.smith
  generated/
    routes.smith
  vendor/
    toml/
      parse.smith
```

The command line is small, and every public function in `project.smith` is also a command.

```
smith new serving            create a project
smith check                  check everything and report warnings, no code generated
smith build                  compile the entry module
smith run                    build and run it
smith test                   run the tests
smith fmt                    format every file
smith vendor add <url>       fetch, hash, and add an entry
smith vendor update <path>   re-fetch one folder and update its hash

smith deploy --env staging   a task from project.smith
```

- A task's parameters are named flags. A `bool` is a bare flag, an `Option` is optional, and there are no positional arguments.
- A `Result` error prints its message and exits non-zero.
- Built-in command names are reserved, so a task named `build` is a compile error.
- `smith build`, `smith run`, and `smith test` fail only on errors. `smith check` also reports warnings, such as dead code, unused variables, and unused imports, and fails if there are any. What each command compiles is described in [compiler.md](compiler.md#structure).
- `smith vendor add` shows a dependency's own vendored list, and writes entries only for what is approved.

## Tests

Tests are found by convention. A file named `x_test.smith` holds tests for `x.smith`, and every function in it named `test_...` is a test. A test returns a `Result`, so a failure carries the values that differed, and several checks chain with `try`. A test file is an ordinary module: it imports with `use` and sees only public names, so tests check behaviour through the interface, while contracts and proofs check the internals. Test files are left out of normal builds.

```
use model.tokenizer (decode, encode)
use std.test (Failed, expect, expect_eq)

fn test_empty() -> Result[(), Failed] {
  expect_eq(len(encode("")), 0)
}

fn test_round_trip(key: take Key) -> Result[(), Failed] {
  text = random_text(key, max: 200)
  expect_eq(decode(encode(text)), text)
}

fn test_tokens(text: str) -> Result[(), Failed] {
  tokens = encode(text)
  try expect(len(tokens) <= len(text))
  expect_eq(decode(tokens), text)
}
```

- A test that takes nothing is a single example and runs once.
- A test that takes a `Key` is a property. It runs many times, building its inputs with ordinary random functions. Under `smith test` the key is backed by a recorded sequence of choices, so a failure is shrunk by replaying the test with smaller choices, and every shrunk input is one the test's own code built.
- Typed parameters are shorthand: each is drawn from a split key with its type's default random function.
- The checker tries to prove a property first and runs it only when it cannot.

```
smith test                           everything
smith test model/gpt_test.smith      one file
smith test --name round_trip         tests whose name matches
smith test --seed 4127               replay a failing run
```

- All tests run in one process, each as a task with its own arena, in parallel.
- Running one test compiles only what that test reaches, so it stays fast in a large project.
- A failure prints the smallest failing input and the seed.
- A test declares its effects like any function. One that touches files gets an in-memory file system unless it says otherwise.

## A transformer

Everything above combines into a decoder-only transformer in under a hundred lines. This version uses one attention head to keep it short.

```
fn softmax[n: Pos](x: [n]f32) -> [n]f32 {
  top = max(x)
  z = sum(for i { exp(x[i] - top) })
  for i { exp(x[i] - top) / z }
}

struct Norm[d: Nat] {
  gain: [d]f32
  bias: [d]f32
}

fn layer_norm[d: Pos](n: Norm[d], x: [d]f32) -> [d]f32 {
  mu = mean(x)
  spread = mean(for c { (x[c] - mu) ^ 2 })
  for c { n.gain[c] * (x[c] - mu) / sqrt(spread + 1e-5) + n.bias[c] }
}
```

Attention has a binding for each intermediate. The causal mask is an `if` on the indices, not a separate array, and `inf` is float infinity.

```
struct Attention[d: Nat] {
  q: Linear[d, d]
  k: Linear[d, d]
  v: Linear[d, d]
  o: Linear[d, d]
}

fn attend[t: Pos, d: Pos](a: Attention[d], x: [t, d]f32) -> [t, d]f32 {
  q = for i { apply(a.q, x[i]) }
  k = for i { apply(a.k, x[i]) }
  v = for i { apply(a.v, x[i]) }
  scale = 1.0 / sqrt(float(d))
  scores = for i, j {
    if j <= i { q[i] @ k[j] * scale } else { -inf }
  }
  w = for i { softmax(scores[i]) }
  mixed = for i, c { sum(for j { w[i, j] * v[j, c] }) }
  for i { apply(a.o, mixed[i]) }
}
```

A block is pre-norm attention and a feed-forward layer, each with a residual connection. The feed-forward layer is the MLP's `forward`, reused unchanged.

```
struct Block[d: Nat, h: Nat] {
  n1: Norm[d]
  attn: Attention[d]
  n2: Norm[d]
  ff: Mlp[d, h, d]
}

fn block[t: Pos, d: Pos, h: Nat](b: Block[d, h], x: [t, d]f32) -> [t, d]f32 {
  normed = for i { layer_norm(b.n1, x[i]) }
  y = x + attend(b.attn, normed)
  renormed = for i { layer_norm(b.n2, y[i]) }
  y + for i { forward(b.ff, renormed[i]) }
}
```

The model embeds tokens, runs the blocks in order, and projects to the vocabulary. Token ids have type `Fin[v]`, so the embedding lookup needs no bounds check.

```
struct Gpt[v: Nat, t: Nat, d: Nat, h: Nat, l: Nat] {
  tok: [v, d]f32
  pos: [t, d]f32
  blocks: [l]Block[d, h]
  out: Linear[d, v]
}

fn logits[v: Nat, t: Pos, d: Pos, h: Nat, l: Nat](m: Gpt[v, t, d, h, l], ids: [t]Fin[v]) -> [t, v]f32 {
  var x = for i, c { m.tok[ids[i], c] + m.pos[i, c] }
  loop n in 0..l { x := block(m.blocks[n], x) }
  for i { apply(m.out, x[i]) }
}
```

No line of this says where memory comes from or how loops run. The compiler stores each intermediate that is read repeatedly, fuses the rest, and compiles the contractions as blocked kernels. Training reuses [Differentiation](#differentiation): a cross-entropy loss over `logits`, its `grad`, and a step function, inside `with device(gpu)`.
