---
okf_version: "0.2"
types: concept
---
# The Smith language

The language as users get it, in the syntax of #1428 only: one concept per idea, each with examples a test compiles. Skills say how to do something and link these pages; a fact lives in one page.

## Concepts

- [What a function writes](contract-modifies.md): contract.modifies names the only objects and ranges a function writes, so smith prove refutes a write outside them and a caller keeps everything else it knew.
- [Contracts](contracts.md): Calls of std.contract that stop the program when a condition is false and that smith prove proves; what requires, ensures, invariant, assert and trusted each mean and where each goes.
- [A program](program.md): The smallest Smith program, a file with an imported module and a main function, which every other concept page builds on.
- [A proof is fast, and fast is CPU time](proof-budget.md): Each proof runs in its own process with about 10 s of CPU time charged as on 8 idle cores and 8 GB of memory, so a busy machine slows a proof and never fails it; the solver's answers are cached.
- [A proof removes the checks it proved](proofs-remove-checks.md): A run of smith prove records the lines of the checks a proved function owns, and an optimized build leaves those checks out while the proof still holds; debug builds keep every check.
- [Proving across calls](prove-calls.md): A callee with an ensures is used through its contract and any other is inlined; pointer parameters, restrict checked at each call, what a caller may assume after a call, returned pointers and lemmas.
- [Reading a cost report](prove-cost.md): When a proof runs out of its budget, and with smith prove -v for every function, the report says where the time went, the slowest queries and what in the code made them costly.
- [The IR smith prove checks](prove-ir.md): smith prove compiles each file to LLVM IR with undefined-behaviour checks; --emit-ir writes the IR it proves, with each contract and check as a numbered point, to explain a message that points at a line you cannot explain.
- [Proving loops](prove-loops.md): A loop without an invariant is unrolled and needs a bound from a constant or a requires; a loop with contract.invariant is unrolled up to 16 passes and proved by induction beyond.
- [Proving slices and arrays](prove-slices.md): A slice carries its count so an index is proved against it; an array parameter is N objects; a pointer view p[a:b] is held to what p is known to point at.
- [Trusted boundaries](prove-trusted.md): contract.trusted() marks a function the prover assumes and never reads, for the edge of a program where the code is not C it can read; callers prove its requires and assume its ensures.
- [What smith prove proves](proving.md): smith prove proves every function with a contract of its own, minus the trusted ones, for every input that meets the requires; how to run it on files or a project and read its verdict lines.
