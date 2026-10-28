---
name: smith-prove-pointers
description: "smith prove and returned pointers: a pointer returned under smith_ensures is a whole aligned object or null, a trusted function's result, variable argument lists, and the cost of a contract paid at every call. Load on a load through a returned pointer failing as a possible null, or when a caller proves slower than the callee inlined."
---

# Returned pointers and the cost of contracts

```smith
module pool;

import std::contract;

export I32 *at(I32 xs[4], Usize i) {
    contract::requires(i < 4);
    I32 *result = &xs[i];
    contract::ensures(result != nullptr);        // without it a caller's load through the result fails as a possible null
    return result;
}
```

- **A pointer a function returns under an `smith_ensures` is a whole object of its type, aligned, or null.** That is proved at each `return` of the function (one past the end of an array, or a pointer to a local, is refused there), and assumed by its callers, which still need `smith_ensures(result != nullptr)` to load through it: without it the load fails as a possible null. A trusted function's result is its promise only: nothing is assumed of the pointer beyond its `smith_ensures`, so a load through it fails; prove the function, or return the value through a parameter. A function with no `smith_ensures` is inlined, and its pointer is whatever its body made.
- A function with a variable argument list (`va_start`) is an error where a proved function calls it; defining one in the program does not stop the others from being proved.
- A contract is paid for at every call: its `smith_ensures` is assumed in each caller's queries. A contract that reads a symbolic board with loops (checkers' exact specification of its move generator) proves its own function in a few seconds, and a caller that uses it can take far longer than the same caller with the callee inlined; give the callee the exact contract and the caller the part it needs, or prove the caller where the callee is inlined.

See `smith-prove-calls`, `smith-prove`.
