---
name: smith-dev-syntax-functions
description: "New syntax (#1428): `fn` declarations with `:` bodies, `-> R`, the last line as the value, `return`, `mut` parameters, parameter defaults (`port: u16 = 443`), named arguments, keyword-only parameters after `;`, `; kw...` and spreading `f(x; opts...)`, `main(args: []str)`, lambdas `fn(x): x * 2`, trailing lambdas `sort(xs) fn(a, b): ...`, trailing blocks `f(x): ...` and `html():` (always `()`), captures, `fn_once`. Load when writing functions, lambdas or calls in the new syntax."
---

# Functions, arguments and lambdas

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Functions and arguments

```smith
pub fn area(r: f64) -> f64:
    TAU / 2 * r * r                 # the last line is the value

fn bump(mut x: i32) -> i32:         # parameters are constant unless mut
    x += 1
    x

fn find(xs: []i32, want: i32) -> ?usize:
    for (i, x) in enumerate(xs):
        if x == want: return i      # return leaves early
    .none

fn divmod(a: i32, b: i32) -> (i32, i32): (a / b, a % b)
fn on_event(_: Event, _: i32): count += 1          # nothing returned: no arrow; repeated _ parameters
pub fn main(args: []str) -> i32: 0                 # or main() with no parameters

fn connect(host: str, port: u16 = 443; timeout: u32 = 30) -> Conn: ...   # after ;: keyword-only
c := connect("example.org", timeout = 5)           # port takes its default
d := connect(host = "example.org", port = 80)      # any parameter may be named

fn info(msg: str; ctx...): ...                     # ; ctx... gathers the leftover named arguments
log.info("parsed rows", count = n, ms = ms)        # ctx is the named tuple (count = n, ms = ms)
log.warn("request failed"; req...)                 # ; x... spreads a struct or named tuple as named arguments
```

- A default must be a compile-time constant. Positional arguments come first, then named ones; a call needs no `;` unless it spreads: `f(1; opts..., retry = 3)`. A name given twice is an error, and with no named arguments `kw` is `()`. Each call site makes its own instance for the names and types it gathers, as a generic does; a call gives every parameter before the `;`'s leftovers (a default is not filled in a call of a function with `; kw...` yet).
- `fn f(x: str, xs...; kw...)`: `xs` gets the leftover positional arguments ([smith-dev-syntax-compile-time](../smith-dev-syntax-compile-time/SKILL.md)), `kw` the named ones.

## Lambdas and trailing lambdas

```smith
scaled := apply(xs, fn(x): x * factor)          # types from the expected fn(i32) -> i32
sort(people) fn(a, b): a.age < b.age            # a trailing lambda: the last argument
gpu.each(&q, out, a, b) fn(o, a, b):            # one element of each per GPU thread
    o.* = a * 2 + b
retry(times = 3): fetch(url)                    # no parameters: f(x): ...
page := html():                                 # and no other argument: still ()
    body():
        h1(class = "big"): "hello"
```

- A last argument that is a function is always written trailing: `smith fmt` rewrites `f(x, fn(a): ...)` to `f(x) fn(a): ...`, and `f(x) fn(): ...` to `f(x): ...`. A statement starting `name:` is a typed declaration; a block call always has `()`. Inside brackets a lambda is one line.
- Captures have no list. A lambda that does not escape captures by reference; a stored one (a field `on_click: fn()`) copies its captures into the current allocator, and the checker refuses one that captures a pointer into the current function's stack. `fn_once` is called exactly once on every path.
- A `fn(...)` parameter is instantiated per lambda, so the call is direct. At the C boundary a lambda with no captures is a C function pointer.

## Not settled yet

`@inline pub fn` with its body in the header (today's `export inline`); for logging (#502), scoped context (like tracing spans) and a bare-name shorthand (`ms` for `ms = ms`).
