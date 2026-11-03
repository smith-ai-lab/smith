---
name: smith-dev-syntax-functions
description: "New syntax (#1428): `fn` declarations with `:` bodies, `-> R`, the last line as the value, `return`, `mut` parameters, named arguments, `main(args: []str)`, one-line lambdas `fn(x): x * 2`, trailing blocks `f(x): ...` and bare `html:`, captures, `fn_once`. Load when writing functions, lambdas or calls in the new syntax."
---

# Functions, lambdas and trailing blocks

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Functions

```smith
pub fn area(r: f64) -> f64:
    TAU / 2 * r * r                 // the last line is the value

fn bump(mut x: i32) -> i32:         // parameters are constant unless mut
    x += 1
    x

fn find(xs: []i32, want: i32) -> ?usize:
    for (i, x) in enumerate(xs):
        if x == want: return i      // return leaves early
    .none

fn divmod(a: i32, b: i32) -> (i32, i32): (a / b, a % b)
fn log(s: str): stdio.printf("%.*s\n", s)          // nothing returned: no arrow
fn on_event(_: Event, _: i32): count += 1          // repeated _ parameters
pub fn main(args: []str) -> i32: 0                 // or main() with no parameters

let c = connect(host = "example.org", retry = true)   // named arguments are optional for functions
```

## Lambdas and trailing blocks

```smith
let factor = 3
let scaled = apply(xs, fn(x): x * factor)       // types from the expected fn(i32) -> i32

retry(times = 3): fetch(url)                    // a trailing block: the last argument, a fn()
repeat(3):
    fmt.print("hello world")
let page = html:                                // no other argument: no parentheses
    body:
        h1(class = "big"): "hello"

let step = fn(x: f64) -> f64:                   // a lambda longer than a line is named first
    let y = x * x
    y + 1
let ys = apply(xs, step)
```

- A trailing block is a `fn()` with no parameters, written `f(x): ...` or bare `name:`; inside a function, `name:` without `let` is always a block call. A last argument that is a `fn()` is always written this way: `smith fmt` rewrites `f(x, fn(): ...)`.
- Indentation is ignored inside brackets, so a lambda inside an argument list is one line; a longer one is declared with `let` first.
- Captures have no list. A lambda that does not escape captures by reference; a stored one (a field `on_click: fn()`) copies its captures into the current allocator, and the checker refuses one that captures a pointer into the current function's stack. `fn_once` is called exactly once on every path.
- A `fn(...)` parameter is instantiated per lambda, so the call is direct. At the C boundary a lambda with no captures is a C function pointer.

## Not settled yet

`@inline pub fn` with its body in the header (today's `export inline`).
