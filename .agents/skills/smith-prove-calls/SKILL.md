---
name: smith-prove-calls
description: "smith prove across calls: a callee with smith_ensures is used through its contract, any other is inlined, pointer parameters, restrict checked at each call, modifies, what a caller may assume after a call, a pointer a function returns, variable argument lists. Load when a proof fails at a call, or on `passes dst (restrict) and src` or `the function writes here, outside the ranges`."
---

# Proving across calls

```smith
module bytes;

import std::contract;

export void copy(U8 dst[restrict :], const U8 src[const restrict :]) {
    contract::requires(countof(dst) >= countof(src));
    for (Usize i = 0; i < countof(src); i++) {
        contract::invariant(i <= countof(src));
        dst[i] = src[i];
    }
}

export void shift_in_place(U8 buf[:]) {
    contract::requires(countof(buf) == 5);
    copy(buf[1:5], buf[0:4]);                    // refused at this call
}
```

```text
FAIL  bytes::shift_in_place
the call of bytes::copy passes `dst` (restrict) and `src` slices or ranges that may overlap
```

`copy(b[0:4], b[4:8])` proves; no contract is needed for it.

- **A call of a function with a `smith_ensures`** proves its `smith_requires` at the call and then assumes its `smith_ensures`. **A call of any other function is inlined**, so its contracts and checks are proved where it is called. A pointer parameter is a valid object of its type (not null, all there, aligned for what the body does, writable if the body stores through it), and a struct passed by value is a value (its bytes are free, never poison). **Two pointer parameters may be the same object or overlap**, whatever their types (C allows `int32_t *` beside `uint32_t *`, and a struct beside a pointer to its member), so a claim that holds only when they are apart fails. What is assumed is what the source declares: `restrict` on a pointer parameter makes it a different object from every other pointer parameter, unless both are only read (`const`). **`restrict` is checked at each call** from a proved function: the object reached through a `restrict` argument must not overlap another pointer argument's object (both only read excepted), (the hidden result pointer of a struct returned in memory is not one) a point that fails at the call naming both parameters (`passes `a` (restrict) and `b` objects that may be the same or overlap`). A pointer is one object of its type; a callee that uses a `restrict` pointer as an array has no range the prover knows, which is an error at its declaration ; take a slice instead, `U8 dst[restrict :]`: it carries its count, the call proves the two ranges apart (`copy(b[1:5], b[0:4])` fails at the call, `copy(b[0:4], b[4:8])` proves; `passes `dst` (restrict) and `src` slices or ranges that may overlap`) and no contract is needed. A trusted callee's calls are not checked.
- **What a caller may assume after a call is the callee's `smith_ensures`, and nothing else the body does.**
  - The memory the call may write is whatever the callee's pointer parameters point to (and what LLVM infers it may reach; a `smith prove -v` note says what it found in the body, a store to a global or a call, where that is not enough; a pure function has none). After the call that memory holds arbitrary values, then the callee's `smith_ensures` is assumed of it, so an `smith_ensures` about memory is about the memory after the call. A local object the call was given is treated the same way.
  - Say what stays the same with `modifies` (smith-contract): a callee that names `&moves->jumps` leaves every other byte of what it was given as it was, and a caller relies on that; without one, say it with `moves->diagonals.len == old_diagonals`. A `const` pointer parameter is not written by the call, and callers may rely on it. A function with `modifies` is used through its contract even with no `smith_ensures`.
  - `modifies` is checked in the body: `FAIL f` / `file:12: the function writes here, outside the ranges its smith_modifies names`. At a call the callee's pointer arguments go to a scratch object and only its ranges are made arbitrary (the callee writes nothing else, being proved to), so the cost follows the ranges, not the object.
  - A caller that relies on something the callee does not promise is refuted with a counterexample (`first = 96` for a byte the callee overwrote), never proved.

- **A lemma is such a call.** A function with a `smith_requires` and a `smith_ensures` and no result (`square_is_nonnegative(n)`) gives its `smith_ensures` at the arguments of the call, once the `smith_requires` is proved there; it may be the only step of the caller's proof. A function called inside a contract (`smith_ensures(twice(n) == 2 * n)`) is part of the claim: its body (a callee with no `smith_ensures` is inlined) or its contract says what its value is. A recursive function is neither proved nor usable through its contract yet (`recursion: ...`, #1054).

A pointer a function returns, variadics and the cost of a contract: `smith-prove-pointers`.

See `smith-prove`, `smith-contract` (`modifies`), `smith-contract-proof`.
