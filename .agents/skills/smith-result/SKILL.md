---
name: smith-result
description: "Using smith::result (no import: result(T, E)): a value or the reason there is none, `{.ok = v}` or `{.error = e}`, in place of an out parameter or a sentinel. Load when code writes `result(`, or on a question about returning an error from a function in .smith."
---

# A value or an error: smith's result

This skill ships in the smith plugin, from `.agents/skills/smith-result/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

`result` is in scope in every module with no import; `smith::result` (`lib/smith/result.smith`, readable source, its header what the compiler reads) defines the one type function, `result(type T, type E)`: an enum with a payload (`smith-lang-enums`), `ok` carrying the `T` and `error` carrying the `E`, which is the caller's own type (a code, an enum, a struct): there is no default error type. `result(f64, i32)` written anywhere is one type. A module cannot declare its own `result` (an error names the built-in).

```smith
module quotient;

import <stdio.h>;

export typedef result(f64, i32) Quotient; // name it at file scope once

export Quotient divide(f64 a, f64 b) {
    if (b == 0) return {.error = 7};
    return {.ok = a / b};
}

export void show(Quotient q) {
    switch (q) {
    case .ok: c::printf("%g\n", q.ok); break;
    case .error: c::printf("error %d\n", q.error); break;
    }
}
```

- A `switch` names every variant (no `default:`); `q.ok` is read only where `q == .ok` holds.
- The struct must be named at file scope before a function body uses it, as for any type function; a generic may write `result(T, E)` in its head and body.
- For a value that may simply be absent use `smith-option`. To return a failure to the caller in one word, `try` (`smith-lang-try`).
