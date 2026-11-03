---
name: smith-dev-syntax-functions
description: "New syntax (#1428): `fn` declarations, `-> R`, the last line as the value, `return`, `mut` parameters, named arguments, lambdas `fn(x) { ... }`, the trailing block `f(x) { ... }`, captures, `fn_once`, and method calls (UFCS) `xs.push(5)`. Load when writing functions, lambdas or calls in the new syntax."
---

# Functions, lambdas and method calls

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Functions

```smith
pub fn area(r: f64) -> f64 {
    TAU / 2 * r * r              // the last line is the value
}

fn divmod(a: i32, b: i32) -> (i32, i32) {
    (a / b, a % b)
}

fn bump(mut x: i32) -> i32 {     // parameters are constant unless mut
    x += 1
    x
}

fn find(xs: []i32, want: i32) -> ?usize {
    for (i, x) in enumerate(xs) {
        if x == want { return i }    // return leaves early
    }
    .none
}

fn log(s: str) {                 // no `-> void` when nothing is returned
    stdio.printf("%.*s\n", s)
}

c := connect(host = "example.org", retry = true)   // named arguments are optional for functions
```

## Lambdas and trailing blocks

```smith
factor := 3
scaled := apply(xs, fn(x) { x * factor })      // types from the expected fn(i32) -> i32

retry(times = 3) { fetch(url) }                // a trailing block: the last argument, a fn()
page := html.render() { html.li(class = "done") { html.text(todo.title) } }

```

- A lambda is `fn(params) { body }`; without a body, `fn(...)` is a type. It matches the declaration `fn name(x: i32) -> i32 { }`.
- Only a function with no parameters can be trailing, written right after the call's `)` on the same line. A last argument that is a `fn()` is always written as the trailing block: `smith fmt` rewrites `f(x, fn() { ... })` to `f(x) { ... }`.
- Captures have no list. A lambda that does not escape (the default) captures by reference. A stored lambda (a field `on_click: fn()`) copies its captures by value into the current allocator; the checker refuses one that captures a pointer or slice into the current function's stack.
- A `fn(...)` parameter is instantiated per lambda, so the call is direct and can be inlined. `fn_once` is called exactly once on every path and may consume what it captured.
- At the C boundary a lambda with no captures is a C function pointer; one with captures passes code and context to APIs that take both.

## Not settled yet

`extern` functions and variables, and a `pub fn` with no body defined in C; `main`'s arguments (`argv`); `@inline pub fn` with its body in the header (today's `export inline`); repeated `_` parameters.
