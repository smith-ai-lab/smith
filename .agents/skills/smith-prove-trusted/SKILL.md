---
name: smith-prove-trusted
description: "smith_trusted(): the prover's boundary, what a trusted function assumes, and the message that names the io, asm or libc call to mark. Load when smith prove says a function calls something with no contract."
---

# Trusted boundaries

The prover reads C. At the edge of a program the code is not C it can read: the io library, inline assembly (random's `register_*` helpers), a libc function (`printf`, `memcpy_chk`), another library with no source in the build. A proved function that reaches one fails: `a call of printf, which has neither a body nor a contract`, with the fix: *put `smith_trusted()` as the first statement of* the function that makes the call (the helper that wraps it, if there is one; the message names it).

```c
static void show(int32_t value) {
    smith_trusted();             // printf is the library's; callers use the contract below, and it is not proved
    smith_requires(value >= 0);
    printf("%d\n", value);
    smith_ensures(value >= 0);
}
```

In `.smith` it is `contract::trusted();` (with `import smith::contract;`), the same as the other contracts.

- A trusted function is **assumed, never proved**: callers prove its `smith_requires` at each call and assume its `smith_ensures`; its body is not read, so assembly and io inside it are fine. The report lists it on its own line, `trusted  show  (file:line)  not proved; callers prove its 1 smith_requires and assume its 1 smith_ensures`, and counts it as `K trusted`, never as proved. `--function=` of a trusted function is an error that says so.
- **What a caller may assume of a trusted function:** it writes only through the pointer parameters it is given (not through `const` ones) and nothing else a proved function can see, and it frees nothing it was not given a writable pointer to. Its `smith_ensures` says what it does to those objects; say what stays the same, as for any callee. A function that really writes a global the callers read is outside what a contract here can state: keep that state out of the proved functions.
- A trusted function with no contract at all marks the edge and promises nothing (a function that prints). Its result, if it has one, is an arbitrary value.
- Mark the smallest function that touches the edge, give it the `smith_ensures` its callers rely on (an index below a count, a result in a range), and prove everything else. The trusted contract is a claim you check by reading and testing; the report keeps the list in front of you.

Also in this family: [smith-contract](../smith-contract/SKILL.md), [smith-contract-c](../smith-contract-c/SKILL.md), [smith-prove](../smith-prove/SKILL.md), [smith-prove-cost](../smith-prove-cost/SKILL.md).
