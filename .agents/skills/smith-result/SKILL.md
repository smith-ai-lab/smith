---
name: smith-result
description: "Using std::result (no import: result(T, E)): a value or the reason there is none, `return v;` or `{.error = e}`, in place of an out parameter or a sentinel. Load when code writes `Result(`, or on a question about returning an error from a function in .smith."
---

# A value or an error: smith's result

This skill ships in the smith plugin, from `.agents/skills/smith-result/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

`result` is in scope in every module with no import; `std::result` (`lib/std/result.smith`, readable source, its header what the compiler reads) defines the one type function, `Result(Type T, Type E)`: an enum with a payload (`smith-lang-enums`), `ok` carrying the `T` and `error` carrying the `E`, which is the caller's own type (a code, an enum, a struct): there is no default error type. `Result(F64, I32)` written anywhere is one type. A module cannot declare its own `result` (an error names the built-in).

```smith
module quotient;

import <stdio.h>;

export typedef Result(F64, I32) Quotient; // name it at file scope once

export Quotient divide(F64 a, F64 b) {
    if (b == 0) return {.error = 7};
    return a / b;                  // a value converts to {.ok = a / b}
}

export void show(Quotient q) {
    switch (q) {
    case .ok: c::printf("%g\n", q.ok); break;
    case .error: c::printf("error %d\n", q.error); break;
    }
}
```

- **A value converts to `.ok`** wherever a `result` is expected (an initializer, an assignment, `return`, an argument), with C's conversions for that context; `{.ok = v}` still works. Only success is implicit: write `{.error = e}`. It is an error, naming the explicit form, when `T` and `E` are the same type (`Result(I32, I32)`: write `{.ok = v}` or `{.error = v}`) or when `T` is itself a `result` or `option`. A `result` never becomes a value (use `try`, `switch` or `r == .ok`), and `r == 5` is an error. In a generic's body write `{.ok = v}`.
- A `switch` names every variant (no `default:`); `q.ok` is read only where `q == .ok` holds.
- `Result(T, E)` may be named first anywhere a type is written, inside a function body too (its struct is defined in the block, the same type as the file-scope one); a generic may write `Result(T, E)` in its head and body.
- For a value that may simply be absent use `smith-option`. To return a failure to the caller in one word, `try` (`smith-lang-try`).
