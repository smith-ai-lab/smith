---
name: smith-contract
description: "Using smith::contract and <smith/contract.h>, smith's contracts: contract::requires, ensures, invariant, assert and trusted (smith_requires, smith_ensures, smith_invariant, smith_assert and smith_trusted in C), checks written as calls, and smith prove, which proves them. Load when code includes <smith/contract.h> or imports smith::contract, when a build.c calls smith_library(b, \"contract\"), when running smith prove, on a question about preconditions, postconditions, loop invariants or assert in C or .smith, or when a proof fails with a cost report, a trusted boundary or `smith prove --emit-ir`."
---

# Contracts: smith's assertions about a function

This skill ships in the smith plugin, from `.agents/skills/smith-contract/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

A contract is a call that stops the program when its condition is false, as `assert` does. The header is `.smith/include/smith/contract.h`; read it first.

**contract is the module `smith::contract`** (`lib/smith/contract.smith`), and a `.smith` file writes `import smith::contract;` and `contract::requires(c)`, `contract::ensures(c)`, `contract::invariant(c)`, `contract::assert(c)` and `contract::trusted()`. C includes `<smith/contract.h>` and calls `smith_requires`, `smith_ensures`, `smith_invariant`, `smith_assert` and `smith_trusted`; the prose below names the calls by their C names. The assert is `contract::assert`, not `assert`: C's `assert` is a macro, and a `.smith` file has no macro (`smith lint` refuses `c::assert` and tells you to write this). A contract is a plain function call, never a macro, so a failure's report is read from the call stack (`smith-trace`).

```smith
module geometry::box;

import smith::contract;

export i32 clamp(i32 x, i32 low, i32 high) {
    contract::requires(low <= high);                     // the caller's job
    i32 result = x < low ? low : x > high ? high : x;
    contract::ensures(low <= result && result <= high);  // this function's promise
    return result;
}

export u32 count_pairs(u32 squares, u32 *moves) {
    u32 len = 0;
    for (u32 square = 0; square < squares; square++) {
        contract::invariant(len <= 2 * square);          // the first statement of a loop body
        moves[len] = square;
        len += square % 2;
    }
    return len;
}
```

| Call | Where | Meaning |
|---|---|---|
| `smith_requires(c)` | the start of a function | a caller must make `c` true |
| `smith_ensures(c)` | just before a `return` (one return, after one `smith_ensures` per promise) | the function makes `c` true |
| `smith_invariant(c)` | the first statement of a loop body | `c` is true on every pass |
| `smith_assert(c)` | anywhere in a body | `c` is true here: the statement `assert` was, as a function. `smith prove` proves it where the function is part of a proof (in a function with a contract, or inlined into one), and a program stops on it in the debug and release profiles; the fast profile compiles it away |
| `smith_trusted()` | the first statement of a function | `smith prove` does not prove this function: callers prove its `smith_requires` and assume its `smith_ensures`, and its body is not read (see "Trusted boundaries"); it checks nothing at run time |

- An old value is a `const` local saved at the top: `const u8 old = moves->len;`. The result is the variable about to be returned.
- A spec is ordinary C: a pure helper function, called inside a contract, with loops of constant bounds.
- **A condition must be free of side effects.** The call evaluates it, and when assertions are off (`target::assertions`, which `-DNDEBUG` turns off: the fast profile) nothing else happens: the body is empty, so the optimizer removes an argument that has none.

## From C

```c
#include <smith/contract.h>

int32_t clamp(int32_t x, int32_t low, int32_t high) {
    smith_requires(low <= high);
    int32_t result = x < low ? low : x > high ? high : x;
    smith_ensures(low <= result && result <= high);
    return result;
}

for (uint8_t square = 0; square < squares; square++) {
    smith_invariant(moves->len <= 2 * square);
    // ...
}
```

## At run time

- A failed contract is `smith_failure(kind)` (`<smith/trace.h>`): it writes `smith-trace begin requires failed` and the stack to standard error and calls `abort()`, at every optimization level; link the trace library (`smith_library(b, "trace")`; `smith_tests` does it itself). In a test it prints the report of a failed check (the failing line and the call chain), and `smith symbolize` turns a saved log into it. Where and what come from the stack and the source line.
- **The static analyzer takes a contract as an assumption.** clang-tidy's `clang-analyzer-*` (`smith lint`) defines `__clang_analyzer__` (`target::static_analyzer`), and there each of the four is `__builtin_assume(condition)`, in every build, `NDEBUG` or not; so `smith_requires(index < N)` stops `clang-analyzer-security.ArrayBound` from reporting that index, as `assert` does, and an index nothing bounds is still reported. A compile, `smith prove` and the checks never see this branch.
- The four are `static inline` functions marked `always_inline` that the header carries (the module's `export inline`, whose `if constexpr` on `target::` is written to the header as the `#if` a C includer's compiler answers), with an empty body when assertions are off. Otherwise each is one call of an external function of the library, `smith_contract_requires`, `smith_contract_ensures`, `smith_contract_invariant` or `smith_contract_assert`, which the optimizer cannot see into. That call stays in the IR at `-O0` and optimized, and it is how `smith prove` finds the contracts without a flag.
- **The library is compiled from source with the project's flags**, so `target::assertions` there is the project's: in the fast profile the library's functions are empty, as the header's inline ones are.

## Get the library in build.c

```c
SmithObject contract = {
    .compiler = "smith-cc",
    .source = smith_library(b, "contract"),
    .output = "build/smith/contract.o",
    .flags = smith_cat(b, flags, smith_list(b, "-I", ".smith/include", nullptr), nullptr),
};
smith_object(b, &contract);
```

Add `-I .smith/include` to the project's own compiles and put `contract` in the program's `.objects`. With `-DNDEBUG` no object references the library, so linking it is harmless. The tests need none of this: `smith_tests` compiles and links the contract library into the runner itself (unless `.objects` already has yours), so a test of code that makes contracts links with nothing added. Only the program's own link needs it.

## Prove them: `smith prove <files> [-- <compile flags>]`

Every full smith has it (`smith build llvm` builds the prover with LLVM); only the stage0, built by `cc` alone, says it has none and exits 2. It is recognised only as the exact first argument.

```sh
smith prove main.c util.c -- -I .smith/include -DDEBUG
smith prove --function=clamp --budget=5 -v clamp.c -- -I .smith/include
```

- **A `.smith` file** is compiled as `smith cc` compiles it (C2y, imports found among the files named, `import smith::contract;` and `contract::requires(...)`), and its functions are named as written everywhere: `ok    geometry::box::clamp  (geometry/box.smith:5)`, `--function=geometry::box::clamp`, `the call of geometry::box::clamp may break smith_requires(low <= high)`, counterexample values `geometry::box::clamp.low = 10`. Lines are the `.smith` source's. A module's imports are other `.smith` files named in the same command, or headers on the include path.
- A project's `smith prove` (no arguments) proves the C and `.smith` sources of each program it builds (`smith_program`; not the tests' runner), together as the program links them, with the flags of their first compiles less what only instruments or caches (`-fsanitize`, modules, frame pointers, `-ftrivial-auto-var-init`, the depfile); it needs no task or list in build.c, and `smith check` runs it. A `.smith` project with no build file (the conventions) works the same. `smith prove -v`, `--budget=SECONDS`, `--emit-ir=FILE` and `--no-cache` are the project's options too. A source the compile rejects with the build's strict flags (a missing prototype) is reported by the compiler, not as "nothing to prove" alone: put the prototypes in a header. With no contract in the files it prints `smith: prove: nothing to prove: ...` and succeeds; a `--function=` that names no function the prover read is an error that names it (each one, before anything is proved), and a function with no contract is one too. A `static` function that nothing calls is not emitted by clang, so a harness written as one is absent: mark it `[[gnu::used]]`.
- **The default set is every function with a contract** (`smith_requires`, `smith_ensures` or `smith_invariant` of its own), **minus the trusted ones**. `main` and any function with no contract are not proved, and nothing says anything of them; a proved function that calls one reads its body, and one with a `smith_ensures` is used through its contract. To have a function proved, give it a contract (`smith_ensures(true)` is enough to ask for its undefined behaviour and asserts).
- Each file is compiled to LLVM IR in smith's own clang: C23, `-O0`, debug info, UBSan's checks for undefined behaviour in trap mode (signed overflow, shifts, division by zero, array bounds with trailing arrays counted, `float` casts, `__builtin_unreachable`), `-UNDEBUG`. The files are linked into one program. The flags after `--` are the project's include paths and defines.
- **`smith new`'s program is the example** (both languages): `primes::sum` (`primes_sum`) has `contract::requires(count <= 5)` (`smith_requires`), a loop, and `contract::ensures(total <= 28)` (`smith_ensures`), so `smith prove` there says `1 proved, 0 failed` in about 0.03 s. A precondition is `requires`, a promise is `ensures`, and neither is `assert`, which alone gives the prover nothing to start from.
- A function is proved when it carries a contract and is not trusted. Each is proved in a process of its own, in at most 10 s (`--budget=`) of CPU time; one that runs out fails like a test that times out. The budget is CPU time so that a busy machine slows a proof but never fails it: a proof is charged what it would take on 8 idle cores (the CPU time of what runs in sequence, plus, for the queries that run side by side, the longest or all of them divided by 8, whichever is more), which is close to its wall time on an idle machine. That process ends at its budget even if the prover is stuck, and when `smith prove` itself is killed. `-v` first prints a `phase` line for each step (read and link, the gate, flatten, verify) with its time and the module's size in instructions, so a stall shows where it is; the gate counts the functions with a contract of their own, trusted ones and ones with only `smith_assert`, and when none has a contract of its own it skips flattening and reports nothing to prove (asserts alone are never a unit). Then `-v` lists every function: the proved ones with their wall time, CPU time and charged time, a proof from the cache as `cached: proved before in ...` with the time it took then, and the trusted ones.
- **A proof is kept by its content.** A function proved once is proved again at once while the IR the prover checks for it (its body, the contracts of its callees, the globals and declarations it uses) is the same: the key is a hash of that IR without line numbers, LLVM's version and a version number of the prover (bumped when a change can alter a proof; never the build time, so rebuilding smith keeps the cache), kept under `$XDG_CACHE_HOME/smith/proofs` (else `~/.cache/smith/proofs`), an entry unused for 31 days removed (checked at most once a week). Only a proof that succeeded is kept. `--no-cache` proves again. The summary says `N proved, M failed, K trusted, J cached`.
- What is proved: every `smith_ensures` and `smith_invariant`, every `smith_assert` and `assert`, every UBSan check, and that nothing else is undefined (an invalid access, a poison value used). `smith_requires` of the function is assumed.
- **A call of a function with a `smith_ensures`** proves its `smith_requires` at the call and then assumes its `smith_ensures`. **A call of any other function is inlined**, so its contracts and checks are proved where it is called. A pointer parameter is a valid object of its type (not null, all there, aligned for what the body does, writable if the body stores through it), and a struct passed by value is a value (its bytes are free, never poison). **Two pointer parameters may be the same object or overlap**, whatever their types (C allows `int32_t *` beside `uint32_t *`, and a struct beside a pointer to its member), so a claim that holds only when they are apart fails. What is assumed is what the source declares: `restrict` on a pointer parameter makes it a different object from every other pointer parameter, unless both are only read (`const`). `restrict` is C's promise, so a caller that breaks it is undefined behaviour the prover does not check at the call; leave it off where a caller may pass the same object twice.
- **What a caller may assume after a call is the callee's `smith_ensures`, and nothing else the body does.**
  - The memory the call may write is whatever the callee's pointer parameters point to (and what LLVM infers it may reach; a note says where that is not enough). After the call that memory holds arbitrary values, then the callee's `smith_ensures` is assumed of it, so an `smith_ensures` about memory is about the memory after the call. A local object the call was given is treated the same way.
  - Say what stays the same: a callee that adds to `moves->jumps` must say `moves->diagonals.len == old_diagonals` for its callers to know the other list was not touched. A `const` pointer parameter is not written by the call, and callers may rely on it.
  - A caller that relies on something the callee does not promise is refuted with a counterexample (`first = 96` for a byte the callee overwrote), never proved.
  - A function with a variable argument list (`va_start`) is an error where a proved function calls it; defining one in the program does not stop the others from being proved.
  - A contract is paid for at every call: its `smith_ensures` is assumed in each caller's queries. A contract that reads a symbolic board with loops (checkers' exact specification of its move generator) proves its own function in a few seconds, and a caller that uses it can take far longer than the same caller with the callee inlined; give the callee the exact contract and the caller the part it needs, or prove the caller where the callee is inlined.
- **Loops are unrolled, and a loop's count comes from a constant or from a `requires` that bounds it.** That bound is the function's worst case (what real-time code needs). A `contract::requires(count <= 5)` (or `0 <= n && n <= 5`: a parameter compared with a constant, alone or joined by `&&`) before `for (usize i = 0; i < count; i++)` makes the loop run at most 5 passes in the proof; a bound on anything that is not a parameter (a field, a computed value) does not reach the unroller, so give the loop a constant or make the bound a parameter. A loop with no visible bound fails with the loop's line, the variable that decides its count and the fix:
  ```text
  FAIL  sum  (primes.smith:8)
    primes.smith:11: cannot be proved: the loop has no bound the prover can see: it runs `count` times
        to prove it: state its worst case, contract::requires(count <= ...), which callers then prove ...
  ```
  A loop that divides or shifts a value to zero (`do { v /= 10; } while (v != 0)`) has no count LLVM can derive, whatever `requires` says; the message says how many passes it takes at most (`a 64-bit value divided by 10 ... at most 20`) and the fix is a counter a constant bounds: `for (usize pass = 0; pass < 20; pass++) { if (v == 0) break; v /= 10; }`. 
- **A loop's passes are unrolled**: `smith_invariant` in an unrolled loop is proved on each pass. Many loads in many passes cost time: a loop of 64 passes over a board takes seconds, and checkers' move generator with its contracts about 2 s.
- A failure prints like a test failure: the contract and its line, the call it breaks, and a counterexample's values by their names in the source.
- **Errors, never assumptions**, each naming the place and what would make it provable: recursion, a call through a function pointer, inline assembly, a function with neither a body nor a contract, an Objective-C message send, a loop with no constant bound, and a function with several returns whose `smith_ensures` is not before one. For the first four (and a libc or io call) the way out is a trusted boundary, below.

## Trusted boundaries: `smith_trusted()`

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

## Errors at their cause, and the IR the prover checks

- `cannot use the contract of X: its smith_requires reads memory ...` names the read and the write with their lines and instructions: `its smith_requires reads memory at f.c:15: \`%2 = load i32, ptr %b2\` after the function has written, at f.c:14: \`store i32 %1, ptr %a1\``. Read the instruction as the prover sees it; a store into a local struct or a `memset` of a local (a zero-initialization) is not a write of the caller's memory and does not count. Put every `smith_requires` before the first write, and every `smith_ensures` after the last.
- **`smith prove --emit-ir=FILE`** writes, for each function it proves, the IR the prover checks: callees replaced by their contracts, helpers inlined, loops unrolled, each contract and each undefined behaviour a call `smith_point(id, condition)`, with the points listed in a comment above the function (`; point 3: smith_ensures, proved: total <= 28  (primes.smith:21)`). It is written for a function that cannot be proved too, as far as the prover got. Read it when a message points at a line you cannot explain.

## Reading a cost report

A proof that runs out of its budget, and `-v` for every function, say what took the time:

```text
FAIL  divmod  (division.c:6)
  over its budget of 2 s of CPU time ...
  where the time went (CPU, as charged):
        0.01 s  writing 3 of the 3 queries as SMT
        1.99 s  the solver
  the slowest of 3 queries (CPU time, side by side on up to 8 threads):
       1.99 s CPU  smith_ensures(q * y + r == x)  (division.c:10)  <- the budget ended here
                    its value depends on a 64-bit multiplication of two variables at division.c:10
                    its value depends on a 64-bit unsigned division by a variable at division.c:8
  the code the solver is given: 12 instructions, 0 loads, 0 stores; what makes queries costly in it:
      division.c:8: 64-bit unsigned division by a variable
```

- **Where the time went** is the CPU time as charged, by phase: Alive2's symbolic execution of the function, writing the queries (one per undefined-behaviour check and contract; a function with thousands of checks spends its budget here, and the budget ends the writing, not only the solving), and the solver.
- **The slowest queries** are named by the contract or the instruction they ask about and its line, with their own CPU time. *Its value depends on* lists the division, remainder and multiplication of variables that its value is computed from, found in the IR (a data dependence, not a guess).
- **What makes queries costly** is read off the IR the solver is given, by line, never estimated: a division or remainder by a variable and a multiplication of two variables (the solver turns each into thousands of gates: divide by a constant, or state the property without it), the loads and stores of a line (an unrolled loop of calls makes hundreds, each a read or write of a memory array), a `memset`/`memcpy` or local object of 128 bytes or more (a by-value zero-init, a copy of a board), and each loop with the passes it was unrolled into (`a loop unrolled into 64 passes`).
- **What to do:** put a `smith_ensures` on the function that does the work in a pass, so that callers use it instead of the body (`fill_square` for `fill_row` for `fill_valid_moves`: a proof of eight calls beats one of sixty-four). Do not repeat a callee's `smith_requires` as a `smith_invariant` just before the call: both are proof points, and the repeat tripled one function's time. Give a callee only the `smith_ensures` its callers need: every `smith_ensures` is assumed in every query of every caller, so an exact specification of a move generator on the generator makes its callers slow; state it on a function that calls the generator and nothing else calls (a proof in the source), as the exact spec of checkers' generators is.
