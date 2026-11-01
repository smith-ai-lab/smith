---
name: smith-lang-try
description: "try and errdefer in .smith modules: `I32 x = try find(s);` returns the failure of a result or an option from the function at once, `errdefer free(p);` runs only when the function fails. Load when a function calls another that returns result(T, E) or option(T) and passes its failure up, or on 'try is a statement of its own' and '`errdefer` runs when the function fails'."
---

# try and errdefer

Every fallible call of a `result` or an `option` takes three lines: store, test, return the failure. In a `.smith` file with a `module` line `try` does it in one, and `errdefer` is a `defer` that runs only on failure (`smith-lang-defer`, `smith-result`, `smith-option`).

```smith
module main;
import <stdio.h>;

static Option(Usize) find(const char *s, char wanted) {
    for (Usize i = 0; s[i] != 0; i++) {
        if (s[i] == wanted) return i;
    }
    return .none;
}

static Result(Usize, I32) offset(const char *s) {
    errdefer c::puts("no offset");             // runs when this function returns an error
    Option(Usize) at = find(s, '=');
    if (at != .some) return {.error = 7};      // a written failure runs it too
    return at.some;
}

static Result(Usize, I32) twice(const char *s) {
    defer c::puts("twice leaves");             // a `defer` already set runs on the way out
    Usize at = try offset(s);                  // on .error: return {.error = e} at once
    return at * 2;
}

I32 main() {
    Result(Usize, I32) a = twice("ab=c");
    Result(Usize, I32) b = twice("abc");
    return a == .ok && b == .error ? 0 : 1;
}
```

## Rules

- **`try`** on a `Result(T, E)` returns `{.error = e}` and otherwise gives the `.ok` value; on an `Option(T)` it returns `.none` and otherwise gives the `.some` value. The return is an ordinary `return`.
- **The function returns the same kind**: a `result` with the same `E` for a result, an `option` (any `T`) for an option. Errors are never converted: to change one, write it out (`if (r == .error) return {.error = wrap(r.error)};`). A mismatch is an error with a note that says so.
- **Where `try` may stand**: a declaration's initializer (`T x = try f();`), an assignment (`x = try f();`), a statement of its own (`try f();`, the value dropped) and a `return` (`return try f();` returns the success value as `.ok` or `.some`, as `return v;` does) and the payload of a returned variant (`return {.ok = try f()};`). It runs first: on failure the function returns at once and the `defer`s run. Inside a larger expression (`g(try f())`, `a + try f()`) it is an error: write a declaration first.
- **`errdefer`** runs like `defer` (its block's exit, newest first) but only when the function returns its failing variant (`.error`, `.none`): from a `try`, from `return {.error = e};` and from `return r;` where `r` holds an error. It needs a function that returns a `result` or an `option`.
- **Keywords only in a module.** Without a `module` line `try` and `errdefer` are ordinary names (`int try = 1;`); in a module they name nothing.
- **In a generic function** the body is kept as written and the kind is checked in each instance: `Result(T, I32) f(constexpr Type T, ...)` with `try g()` is fine for an instance whose `g` returns a `result` with that `E`, and an error for one that does not (the error names the instance). Name the result at file scope first (`typedef Result(Usize, I32) Wide;`), as for any generic that returns one (`smith-lang-generics`).

Overview: `smith-lang`. Reference: [knowledge/smith-language.md](../../../knowledge/smith-language.md) ("try and errdefer").
