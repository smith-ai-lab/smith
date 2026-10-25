---
name: smith-lang-try
description: "try and errdefer in .smith modules: `i32 x = try find(s);` returns the failure of a result or an option from the function at once, `errdefer free(p);` runs only when the function fails. Load when a function calls another that returns result(T, E) or option(T) and passes its failure up, or on 'try is a statement of its own' and '`errdefer` runs when the function fails'."
---

# try and errdefer

Every fallible call of a `result` or an `option` takes three lines: store, test, return the failure. In a `.smith` file with a `module` line `try` does it in one, and `errdefer` is a `defer` that runs only on failure (`smith-lang-defer`, `smith-result`, `smith-option`).

```smith
module main;
import <stdio.h>;

static option(usize) find(const char *s, char wanted) {
    for (usize i = 0; s[i] != 0; i++)
        if (s[i] == wanted) return {.some = i};
    return .none;
}

static result(usize, i32) offset(const char *s) {
    errdefer c::puts("no offset");             // runs when this function returns an error
    option(usize) at = find(s, '=');
    if (at != .some) return {.error = 7};      // a written failure runs it too
    return {.ok = at.some};
}

static result(usize, i32) twice(const char *s) {
    defer c::puts("twice leaves");             // a `defer` already set runs on the way out
    usize at = try offset(s);                  // on .error: return {.error = e} at once
    return {.ok = at * 2};
}

i32 main() {
    result(usize, i32) a = twice("ab=c");
    result(usize, i32) b = twice("abc");
    return a == .ok && b == .error ? 0 : 1;
}
```

## Rules

- **`try`** on a `result(T, E)` returns `{.error = e}` and otherwise gives the `.ok` value; on an `option(T)` it returns `.none` and otherwise gives the `.some` value. The return is an ordinary `return`.
- **The function returns the same kind**: a `result` with the same `E` for a result, an `option` (any `T`) for an option. Errors are never converted: to change one, write it out (`if (r == .error) return {.error = wrap(r.error)};`). A mismatch is an error with a note that says so.
- **Where `try` may stand**: a declaration's initializer (`T x = try f();`), an assignment (`x = try f();`), a statement of its own (`try f();`, the value dropped) and the payload of a returned variant (`return {.ok = try f()};`, `return {.some = try f()};`). It runs first: on failure the function returns at once and the `defer`s run. `return try f();` is the ordinary return of the success value, so it is the error `return v;` is when the function returns a result. Inside a larger expression (`g(try f())`, `a + try f()`) it is an error: write a declaration first.
- **`errdefer`** runs like `defer` (its block's exit, newest first) but only when the function returns its failing variant (`.error`, `.none`): from a `try`, from `return {.error = e};` and from `return r;` where `r` holds an error. It needs a function that returns a `result` or an `option`.
- **Keywords only in a module.** Without a `module` line `try` and `errdefer` are ordinary names (`int try = 1;`); in a module they name nothing.
- **In a generic function** the body is kept as written and the kind is checked in each instance: `result(T, i32) f(constexpr type T, ...)` with `try g()` is fine for an instance whose `g` returns a `result` with that `E`, and an error for one that does not (the error names the instance). Name the result at file scope first (`typedef result(usize, i32) Wide;`), as for any generic that returns one (`smith-lang-generics`).

Overview: `smith-lang`. Reference: [knowledge/smith-language.md](../../../knowledge/smith-language.md) ("try and errdefer").
