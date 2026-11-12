---
name: smith-prove
description: Prove C or .smith code with smith prove. Use for contracts (requires, ensures, invariant, modifies, trusted), a failing proof, loop bounds and invariants, slice or array indexing, restrict at a call, a proof over budget or memory, and proved checks in release.
---

# Prove a function

Every answer about a proof starts by opening the page that matches the failure (step 3), because the prover's rules are exact and not guessable; answer from that page. The facts for the commonest case:

- A function is proved when it has a contract of its own and is not trusted; `requires` is assumed, everything else is proved.
- Prove a project (`smith prove`, no arguments); a file with a loop invariant fails in the file form.
- A precondition is `requires`, a promise `ensures`, a loop fact `invariant` (first statement of the body), a fact in the middle `assert`.

## Steps

1. Write the contracts for the function: [contracts](../../knowledge/contracts.md). A precondition is never an `assert`.
2. Run `smith prove`; add `-v` for each function's time and each loop's mode.
3. Read the failure line and open the page for its kind (run and read lines: [proving](../../knowledge/proving.md)):
   - `the loop has no bound the prover can see`, or `may be false after a pass`, `when the loop is entered`: [prove-loops](../../knowledge/prove-loops.md).
   - `an index or a slice is in range`, an array or slice parameter: [prove-slices](../../knowledge/prove-slices.md).
   - A failure at a call, `restrict`, `passes dst (restrict) and src`, a returned pointer, a lemma: [prove-calls](../../knowledge/prove-calls.md).
   - `the function writes here, outside the ranges`, or a caller that loses a field: [contract-modifies](../../knowledge/contract-modifies.md).
   - `a call of X, which has neither a body nor a contract`, asm, libc: [prove-trusted](../../knowledge/prove-trusted.md).
   - `over its budget`, `over its memory limit`, slow: [proof-budget](../../knowledge/proof-budget.md), then [prove-cost](../../knowledge/prove-cost.md).
   - `cannot use the contract of X`, a line you cannot explain: [prove-ir](../../knowledge/prove-ir.md).
4. Run again until `smith prove: ok`; the coverage line says how many functions have a contract, and `smith prove -v` lists the rest.
5. Release builds drop the proved checks: [proofs-remove-checks](../../knowledge/proofs-remove-checks.md).

Check against the code, not memory: `smith prove --help`.
