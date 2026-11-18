---
type: specification
title: The Smith compiler
description: How the compiler is designed. It covers how for expressions and effects are compiled, the compiler's structure and commands, its representation, backends, what an executable depends on, testing, timings, releases, and bootstrap.
---

# The Smith compiler

Smith's compiler is an optimizing compiler built around `for` expressions. Its optimizations are fixed rules keyed on shape and structure, with no search and no heuristics that vary between builds, so performance is predictable and builds stay fast. It emits machine code and GPU shaders itself, with no LLVM.

## How `for` is compiled

- **Fusion.** An array read once per element fuses into its consumer. An array read repeatedly, or by several consumers, gets a buffer.
- **Contractions.** A `for` with a `sum` over a shared index, which is what every `@` expands to, is recognized and compiled as a tuned library would write it: loop order chosen, blocked for the cache, with a vector kernel inside.
- **Strategy by size.** Type parameters are always specialized. For size parameters, a shared version always exists that takes the sizes at run time and picks its strategy with a size check: a simple loop, or cache blocking and threads. A specialized copy is made only when the sizes are constants and the operation count at those sizes is under a fixed bound, as for a 4 by 4 matrix, which is then fully unrolled into SIMD registers. The rule uses no profile data, and `smith cost` reports which version a call uses.
- **Whole-function vectorization.** A scalar function called inside a `for` runs across SIMD lanes or GPU threads, with branches turned into masks.
- **Reductions.** Each core runs several SIMD accumulators, cores combine their partial results, and a GPU uses a tree. The grouping is fixed for a given compiler and target and never depends on thread count, so results are repeatable. A reduction that fills one element of an array runs in order along its index.
- **Layout.** No address is observable, so the compiler is free to store an array of structs as one array per field.
- **Devices.** Inside `with device(gpu)`, each fused loop nest becomes a GPU kernel.

## Scalar form

Below the kernel form, every program is scalar code in one small typed form, which is the same for every target. A feature of the language is lowered to it once, and no target's code changes when a feature is added. The compiler's own code and the edges of a server are scalar from the start, and a kernel reaches this form as explicit vector operations.

- **Every value is named once.** A function is a list of blocks. A block takes parameters, runs its instructions in order, and ends by jumping to a block with arguments, by choosing between blocks, or by returning values. An instruction gives a value that never changes, which is the form known as SSA, and a block's parameters are how values meet where paths join.
- **Every value has a type and a size:** an integer of 8, 16, 32, or 64 bits, a float of 16, 32, or 64 bits, an address, or a vector of a fixed number of one of those. Whether an integer is signed belongs to the operation, not to the value.
- **Nothing is a group.** A struct or a tuple is its parts, each a value of its own. A text is an address and a length. An enum is the number of the case it holds, in one byte, and then places that its cases share by type. For each scalar type the enum has as many places of that type as the case that needs the most of them, so an enum whose cases each hold one text has one address and one length, however many cases it has. A case's parts are split into scalars in the order written, and the k-th scalar of a type goes to the k-th place of that type. The types are taken in the order they first appear among the cases' scalars, cases in order, so `Option[str]` is a case number, an address, and a length, and an enum with a case that holds a `bool` and a case that holds a text is a case number, a byte, an address, and a length. A place that the case a value holds does not use holds the zero of its type, and nothing reads it. No address is observable in Smith, so nothing forces a value into memory, and a struct lives in registers like any other values. Memory is for what has a size known only at run time, such as an array's elements, and is read and written through an address at the element's real size.
- **The instructions are few:** constants, arithmetic, bit operations, shifts, and comparison at each width, widening and narrowing, a choice between two values, a read and a write through an address, a call of a function, which may give back several values, and a call into the system.
- **Inlining replaces a call of a small function with the function's instructions.** It is the first pass, a step of its own between the lowering and the targets, which neither knows of. A call is replaced when the function is called from one place only, or when the function's size is at most 4. A function's size is the number of its instructions plus the number of its blocks, one for how each block ends. Functions are taken callees first, so a function is as small as it will be when a call of it is looked at, and only calls from the functions `main` reaches are counted. The caller's block is cut where the call was: what came before jumps to a copy of the callee's blocks, whose returns jump to a block that takes the call's results and holds what came after. A function that nothing calls is left out. The bound is the one that gave the smallest executables for the cases, as a function of the bound, on the targets whose executables change with their code, and nothing measured from a run is used. The pass does one walk, repeats nothing until it stops changing, and gives the same program every time.
- **Folding replaces an instruction whose inputs are all known by its answer.** It is the second pass. A value is known when it is a constant, or a parameter that every jump into its block gives the same constant, or a choice made on a known value. An instruction that reads no memory and calls nothing, whose inputs are known, becomes a constant. A choice between two values on a known value is the value it chooses, and a block that ends by choosing between blocks on a known value ends with a jump. The answer is worked out by the code the evaluator uses, so the two cannot disagree. The pass does one walk over each function, each block after the blocks that jump to it except by a jump back, and a parameter of a block that is jumped to by a jump back is not known. An address is moved by a number of bytes at run time, and the form has no constant for one moved, so that is left.
- **Working out the same thing once replaces a second by the first.** It is the third pass. An instruction that reads no memory, writes none, and calls nothing is the same as an earlier one when it does the same to the same values and gives the same type, and a constant counts, so a constant is made once in the blocks its first making reaches. The second is taken away and its value is the first's when the first is in a block that every path to the second's block passes through, or earlier in the same block. The pass does one walk over each function's tree of dominators, and an instruction whose inputs were replaced is found in the same walk. A read through an address is never replaced, as the form does not say what a write may change. Two instructions that do the same to the same values in the other order, such as `a + b` and `b + a`, are not the same.
- **Removal takes away what nothing uses.** It is the fourth pass. In one walk for each step, it takes away the blocks that nothing reaches, then reads a parameter that every jump into its block gives the same value as that value, then takes away each instruction whose values nothing uses, unless it writes memory, calls a function, or calls into the system, and each parameter that nothing uses, with what every jump gave it, and last joins a block that is jumped to once, by a block that ends with a jump, to that block. A value is used when a return, a choice between blocks, an instruction that stays, or a jump that gives it to a parameter that is used reads it, so a value a loop only gives on to itself is not used. A loop's parameters that stand for one another, such as two that start as the same value and swap, are left.
- **The passes are a fixed list,** each roughly linear in time, run in this order: inlining small functions, folding constants, working out the same thing once, and removing what nothing uses. Nothing iterates to a fixpoint, and value semantics means no alias analysis is needed. Each pass takes the form to the form, so a pass is tested by running a program before it and after it.
- **A target is thin.** For each kind of chip, instruction selection picks instructions for the form's operations, and tuning for that chip happens there. One register allocator serves every chip and is told its registers: values live in registers, and only what does not fit goes to the frame. Calls between Smith functions pass arguments and results in registers, by one convention for each kind of chip on every system, and only a call into the system follows the system's convention.
- **Registers are offered by kind.** A chip tells the allocator its registers in two lists: those a call may change, and those it does not. It keeps for itself only what instruction selection cannot do without. The allocator gives each value one home for its whole life, a register or a place in the frame. A value that is alive across a call takes a register the call does not change, or a place in the frame. Any other value takes a register a call may change first, so a function that calls nothing saves and restores nothing. A function saves a register a call does not change, in its prologue, only when it uses one. A call into the system counts as a call, and it may change every register a call may change, taking the widest of the three systems of the chip, so the three targets of a chip share their code. This is Smith's own convention, one for each kind of chip on every system.
  - **x86-64** passes the first five arguments, and the first five results, in `rdi`, `rsi`, `r8`, `r9`, and `r10`, in that order. It keeps `rax`, `rcx`, and `rdx`, which multiplying and comparing need, `r11`, which holds the value that breaks a circle of copies, and `rsp`. A call may change `rdi`, `rsi`, `r8`, `r9`, and `r10`. A call does not change `rbx`, `rbp`, `r12`, `r13`, `r14`, and `r15`.
  - **arm64** passes the first fifteen arguments, and the first fifteen results, in `x0` to `x14`, in that order. It keeps `x15`, which reaches a place in the frame that is far from the stack pointer, `x16` and `x17`, which instruction selection works in, with `x17` also holding the value that breaks a circle of copies, `x18`, which a system may own, `x29`, `x30`, and the stack pointer. A call may change `x0` to `x14`. A call does not change `x19` to `x28`.
- **A call passes values in registers.** For each kind of chip one convention, the same on every system, names which registers carry a call's arguments, in order, and which carry its results. They are among the registers a call may change, so no value alive across a call is in one. The k-th argument is in the k-th of them, and the k-th result is in the k-th of them, and an argument or result after the last goes through the frame, at its size. The allocator is told how many registers carry them, and gives a parameter that arrives in a register that register when no value alive holds it and the parameter is not alive across a call, so a function that uses its parameters where they arrive copies nothing. A call moves its arguments to their places, and takes its results from theirs, as a jump gives its values, as if all were copied at once.
- **The form runs.** It has an evaluator, which says what a program in it does, and that is what each target's executables are tested against.

The idea of keeping several levels, and lowering through them one step at a time, is the one MLIR is built on. Smith borrows the idea and none of the code. Each level here is its own small type with its own evaluator, so every step from one level to the next has the same test: a generated program does the same thing before the step and after it.

## Effects

Effects are checked and then erased, so a permission costs nothing at run time.

- **A handler is resolved at build time.** The handler in force at each call is known from the source, so a function is compiled once for each handler it runs under and its operations become direct calls. Under the operating system's handlers they are system calls.
- **Start-up code installs the real handlers.** The entry point of an executable is Smith code from the standard library. It installs the operating system's handlers for the effects `main` lists and calls `main`. No part of the compiler or the library reaches the system by any route a program could not use.
- **Suspension is a property of the copy.** The copy of a function that runs under an async handler is a state machine, and the copy under a blocking handler is ordinary code.
- **Exclusion is added only where tasks can meet.** When a task group opens under a handler whose operations assign a `var`, the runtime makes those operations take turns. A handler with no tasks under it has no such cost.
- **A handler's state has its own arena,** covered by the same exclusion, so a child task that adds to it never allocates in its parent's arena.

## Structure

The compiler is a pure function from a set of roots and the source text to results: diagnostics, types, and optionally a binary. Nothing is kept between runs, so every run starts clean and no result can be stale.

It works in three steps, each a pure function of its inputs with no shared mutable state.

1. **Parse** a file. This depends on that file's text alone.
2. **Collect signatures** from the parsed files.
3. **Check a body** against the signatures. A signature states everything a caller needs, so checking one body never depends on another body, and bodies check in parallel.

The compiler is lazy. It starts from the roots and does only the work they reach. A module is a file named by its path, imports are explicit lists, and a trait implementation lives with the trait or the type, so the compiler finds a name's file without scanning the project. Files that nothing reaches are never read.

| Command | Roots |
| --- | --- |
| `smith build`, `smith run` | `main` |
| `smith test --name x` | That test |
| `smith test` | Every test |
| `smith check` | `main`, every test, and a library's public items |

- `smith run prog.smith one two` gives the program the words after the file's name, `one` and `two`, as the words it was started with, whatever they look like, so a flag among them is the program's and not the compiler's. A flag before the file's name is the compiler's. `smith build` takes no words. The words are given as the system gives them, whatever bytes they hold, so a word that is not valid UTF-8 ends the program as it ends the executable, with the same output and status. An argument of the compiler's own, such as the file's name, that is not valid UTF-8 is an error in the usual form, and `smith` never ends in a panic over its arguments.
- Checking a body needs only the signatures of what it calls. Running it needs those bodies too, all the way down.
- Code that no root reaches is not compiled, so it can hold an error while a single test runs. That lets a new design be tried with one test before every caller is updated. The error surfaces when a command whose roots reach it is run.
- `grad(f)` needs the body of `f` and of everything it calls. Its type depends only on the signature of `f`, so checking stays local, and whether `f` can be differentiated is decided when the code is built.

### Errors and warnings

An error stops every command. A warning is reported only by `smith check`, which fails if there are any, so it is the lint step and a continuous integration run needs no extra flag.

What the compiler prints is written to be read by a model, and there is one form of it, for an error and for a warning alike. An error gives its place, what is wrong, the line of source with a mark under the place, and what to write when the compiler knows.

```
hello.smith:1:13: `print` needs the `console` effect, which `main` does not declare
  1 | fn main() { print("hi") }
    |             ^
  fix: write `fn main() ! console`
```

- The first line is `file:line:column: message`. Lines and columns count from 1, and a column counts characters.
- The line of source is shown as written, except that a tab is shown as a space and any other control character as `�`. The mark is placed for a terminal, where a wide character takes two places. Advice is only ever on the `fix:` line, and an error with none has no such line.
- Every error in a file is reported, each once, in the order of their places, so one round of changes can fix them all.
- A file is read in stages: its text, which must be UTF-8 with no carriage return, then its tokens, its syntax, and the check. Only the errors of the earliest stage that finds any are reported, because each later stage reads what the earlier ones produced.
- An error that another error explains is not reported. A mistake in the syntax hides the rest of its statement, or of its signature, and a listed effect with no such name hides a missing effect, which it may be a misspelling of. A function that lacks an effect is told so once, at the first call that needs it, and nothing is reported about what a call of a function that does not exist gives.

- Dead code, meaning a definition no root reaches, whether a function, a struct, a trait, or an implementation, is a warning at its name.
- A binding whose name is never read is a warning at the name, and so is an unused import. A parameter is not a binding.
- A warning is printed in the form above, with a `fix:` line when the compiler knows what to write. Warnings are reported only when there is no error, every one of them, in the order of their places.
- An unused value whose type cannot be discarded is an error, as [Ownership](language/ownership.md) requires.

### Commands that report

The command line is the compiler's interface for people and for agents, and its output is plain text in the one form above.

| Command | Reports |
| --- | --- |
| `smith check` | Errors with a counterexample, the remaining proof goal, and candidate fixes, plus warnings |
| `smith hole` | The type, effects, and proof goal a `hole(name)` needs |
| `smith find` | Definitions matching a type signature, so existing code gets reused |
| `smith deps` | What a definition uses, and what uses it |
| `smith cost` | Allocation and operation counts as formulas over the sizes, and which arrays were stored |

## Representation

The pipeline is the classic one: tokenize, parse, collect signatures, check, lower, run the passes over the scalar form, and emit. A run that only checks stops after the fourth step.

The lexer and parser are hand-written: recursive descent for declarations and a Pratt parser for expressions. One syntax tree serves the checker and the formatter. Every node keeps its source span, comments are ordinary nodes attached to what follows them, and an error node holds any tokens the parser skipped, so broken programs and holes are still representable.

| Level | What happens there |
| --- | --- |
| Surface tree | Parsing and formatting |
| Typed core term | Checking and `grad`. About ten node kinds |
| Kernel form | Fusion, contractions, and vectorization. Shared by the CPU backend and both GPU emitters, which leave from here |
| [Scalar form](#scalar-form) | Inlining, folding, sharing what is worked out twice, and the removal of dead code. The same for every chip |
| Machine form | Instruction selection and register allocation, for one kind of chip |

## Backends

| Target | Output |
| --- | --- |
| CPU | arm64 and x86-64 machine code, written to Mach-O, ELF, and PE directly |
| Apple GPU | Metal shading language source, compiled by the OS at load time |
| Vulkan GPU | SPIR-V binary |

Everything up to the kernel form is the same for every target. Each CPU target supplies its instruction selection, its calling convention, and its executable writer, and that seam exists from the first program. The compiler writes executables itself and runs no linker or assembler.

A target is one case of an enum, and adding a case is what adds a target: the compiler then requires every place that depends on the target to handle it. A target has one name, which is its case in the `Target` enum that `build` takes in `project.smith`, and the same word selects it on the command line, as in `--target linux_x64`.

- **Linux on x86-64,** named `linux_x64`. A static ELF executable that talks to the kernel directly.
- **macOS on arm64,** named `macos_arm64`. A Mach-O executable that reaches the kernel through the system library, which it links dynamically, and that carries the ad hoc code signature Apple silicon requires.
- **Linux on arm64,** named `linux_arm64`. A static ELF executable that talks to the kernel directly.
- **macOS on x86-64,** named `macos_x64`. A Mach-O executable like that of `macos_arm64`, for an Intel Mac.
- **Windows on x86-64,** named `windows_x64`. A PE executable for the console that reaches the kernel through `KERNEL32.dll`, which it links dynamically.
- **Windows on arm64,** named `windows_arm64`. A PE executable like that of `windows_x64`, for an arm64 machine.

Targets share what they have in common: the arm64 instructions are written once for every arm64 target, the x86-64 instructions once for every x86-64 target, the ELF writer once for both Linux targets, the Mach-O writer once for both macOS targets, and the PE writer once for both Windows targets.

Everything runs as native code, including compile-time code such as literal conversion. The checker contains a small symbolic evaluator that simplifies expressions with unknowns, such as `n + 0`, to compare types. It defines what core terms mean, so it is also the test oracle.

## Standing alone

A Smith executable carries everything it runs. All of its code is compiled from Smith source into the one file, in the same way that outside code is [vendored](language/projects.md) into the project and never fetched from a shared cache. The only thing the executable needs from the machine is the operating system's own stable boundary, and that boundary is the kernel wherever the kernel allows it.

| System | Boundary | Why |
| --- | --- | --- |
| Linux | The kernel's system calls, made directly | The kernel keeps them stable, so the executable is fully static and uses no C library |
| macOS, iOS | `libSystem`, loaded by the system's dynamic loader | Apple changes system call numbers between releases, and Apple silicon does not run a static executable |
| Windows | `KERNEL32.dll`, loaded by the system's loader | Windows changes system call numbers between builds |

- From a system library a program calls only the thin wrappers over the kernel, such as `write`, `mmap`, and `exit`, or on Windows `WriteFile` and `ExitProcess`. It never uses the C library behind them. Allocation, formatting, and everything else are written in Smith and behave the same on every target.
- Nothing from the system is needed to build. The compiler writes the records that ask the loader for a library and its symbols, so it needs no SDK and can build for any target from any machine.

## Why builds are fast

- **Only what is reached is compiled.** A run costs what its roots use, not the size of the project or the standard library.
- **Bodies check independently.** Signatures are complete, so bodies are checked in parallel across every core.
- **Rules, not search.** Every optimization above is a fixed rule.
- **Proofs are checked, not found.**
- **No cache.** There is nothing to invalidate, download, or go stale.

## Platform layer

Each platform needs a small set of foreign bindings for a window, a GPU surface, input, and audio, all through the C calling convention. Bindings are generated and validated with Clang at development time, so Clang never ships with the compiler.

| Platform | Window and input | GPU | Linking |
| --- | --- | --- | --- |
| macOS, iOS | AppKit or UIKit through the Objective-C runtime's C functions | Metal | Dynamic to system libraries. iOS builds are ahead-of-time only |
| Linux | Wayland or X11 socket protocol | Vulkan | Static possible without a GPU |
| Windows | Win32 through system DLLs | Vulkan | Dynamic to system DLLs |
| Android | NDK native activity, JNI for the rest | Vulkan | Dynamic, plus a manifest |

## Testing

The test suite is a few properties per layer and a small regression corpus, with almost no unit tests. Tests touch only the compiler's commands and semantic properties, so refactors do not break them.

For the parser, one generator walks the grammar and builds a random derivation tree, with shrinking. The tree is flattened to program text and also folded into the expected syntax tree, so no second parser is needed.

1. Parsing generated text gives the expected tree.
2. Formatting is idempotent and preserves every token.
3. Mutated input never crashes or hangs, and anything the parser accepts passes property 2.

The other layers use a second generator that builds well-typed programs.

| Layer | Property |
| --- | --- |
| Checker | A well-typed program never gets stuck in the evaluator, and its result has the declared type |
| Optimizer | Fused, blocked, and vectorized code gives the same result as the plain loop nest |
| Backends | Native output equals evaluator output on small inputs. GPU equals CPU within the stated float tolerance |
| Differentiation | `grad` matches finite differences |
| Laziness | For any root, the lazy result equals what a run over every root gives for the same definitions |
| Determinism | The same input gives the same bytes |
| Registers | For every generated function and every set of registers offered, from none to more than it needs, no two values alive together share a home, and no value alive across a call is in a register the call may change. A function that calls nothing and has no more values alive together than registers keeps every value in a register and nothing in the frame. A parameter that arrives in a register, and is not alive across a call, is kept in it when it is in a register at all |
| Specialization | A program with type parameters builds to the bytes of the same program written out with one function and one struct for each type it uses |
| Defaults | A program that leaves arguments and fields to their defaults builds to the bytes of the same program with them written out |
| Cases | A program that writes cases with a leading dot builds to the bytes of the same program with every case written in full |
| Repr | The text the `Repr` of a value gives, put in the value's place, builds an equal value |
| Integers | What a program prints of an integer, of a comparison, and of `wrapping_`, `saturating_`, and `checked_` arithmetic is what arithmetic on 128 bits gives for the same values |
| Arithmetic proofs | A plain `+`, `-`, or `*` on two numbers compiles exactly when the true result fits the type, and prints it. Inside the conditions that bound its operands, in every form they are written in, it compiles exactly when the result fits for every value inside them, and gives the true result at and next to the bounds. For every set of facts and goal over a few variables with small ranges, `arith` says yes exactly when trying every value finds none that makes the facts true and the goal false. An accepted operation adds one instruction to the program that gives its left operand instead |
| Loops | A `loop i in a..b` runs its block once for each whole number from `a` up to but not including `b`, in order, at every limit of every integer type, and not at all when `a >= b`. A `break` ends the nearest `loop` and no other. In the block, `arith` knows `a <= i` and `i < b`. A loop over a range is one counter and one comparison each time round in the scalar form, with no call and no memory asked for |
| Arguments | For every list of words, each of printable ASCII with spaces among them, a program that prints each of its `args` on its own line prints exactly those words in order, in the evaluator and from the executable of every target the machine can run, and the program's own name is never one of them. A word that is not valid UTF-8, among words of any bytes, ends the program before `main` with the one line that says which, as the bytes of Rust's own check of UTF-8 say. A Windows command line made from the words by the quoting the system's own programs read back is split into those words. A program whose `main` takes no parameter has no code for the words |
| Given traits | A program that prints and compares no struct, enum, or tuple builds to the bytes of the same program with `without` taking every given trait away |
| Self-hosting | The compiler built by itself twice is byte-identical |

- Each property is a theorem to prove later, so it is written in that form now.
- Specific cases live as `.smith` files with expected output beside them, updated by one command and reviewed as a diff. In the bootstrap compiler the command is `SMITH_UPDATE=1 cargo test cases`, run in `bootstrap/`.
- A generator draws every choice from the seed of the run, which is made from the commit that is checked out, so the cases differ from one commit to the next and are the same for one commit, and many runs cover more than one run could. A run prints its seed first, and a failure prints it again, so that `SMITH_SEED=<seed> cargo test` runs the same cases. A failing input is shrunk, by shrinking those choices, before it is reported. The fingerprint below is made with seed 0, so that it changes only when the executables the tests run do.
- Tests run in-process and in parallel.
- **A test computes for less than a second, and a run fails and names one that does not.** What is measured is the processor time of the thread that ran the test, not the time on the clock, so the verdict is the same alone and beside every other test, and a busy machine does not fail a quick test. The run fails on Linux on x86-64, the machine the `linux` job of `ci` uses, where every test runs, and on any other machine prints a slow test and passes. A test that starts a program or a thread has not spent the time of either, so tests call a function where one can be called.
- **What keeps the tests fast:** the compiler and the tests are built with the first level of optimization, `opt-level = 1` for the `dev` profile, which makes them four times faster than a build with none; a property runs as many cases as make it fail when its break is made, and no more; and work that tests share, such as the programs lowered for the scalar form, is done once in a run.
- **A program that never ends is a failing test.** The evaluator stops a program that has entered three million blocks, or has two thousand calls open, and the test fails with a message that says the program never ends. A program a test starts, the compiler included, is killed when it has run for ten seconds on the wall, and the test fails with the same message. Any other test that has not ended after twelve seconds on the wall fails, the run names it, and the run ends without it. A property that fails stops shrinking its input after four seconds on the wall and reports the smallest failing input it has, so that its own message is what the run shows. The wall is what a busy machine stretches, so ten seconds stops a program that never ends and never a program that ends.
- **The limit applies to every test.** A test that starts a thread has not spent the time of it, so a test starts none, and the work that `needs-machines.sh` is tested with is made of one small test of the machine group, not the whole of it.
- **The sweep shows what the tests catch.** `bootstrap/sweep/sweep.sh` makes small changes to the compiler's source, one at a time, and runs every test against each. `sh sweep/sweep.sh run` in `bootstrap/` runs it, with the changes in `bootstrap/sweep/changes`, which a hash of the file, the line, and the operator picks, and writes `bootstrap/sweep/results`; `sh sweep/sweep.sh report` says which changes each test alone catches. A change that every test passes, and a change that makes the tests hang, are what the sweep finds. It changes `bootstrap/src` only, so it says nothing of the tests of `.github` and `install.sh`. No test is removed on its evidence, as two tests that catch the same break would each catch nothing alone.
- A test states its expectation from the generator that built its input, never from a line or column written by hand, so adding a feature does not break it.

Every target passes the same tests, which take their targets from the target enum's own list of cases, so adding a case runs them over it and no test names a target. A test that builds skips a target the compiler cannot build for yet, a test that runs the executable skips a target this machine cannot run, and a run names each target it did not run.

| Property | How it is checked |
| --- | --- |
| Building twice gives the same bytes | For every target the compiler can build for, on generated programs |
| The executable does what the evaluator says the program does | For every target this machine can run, on generated programs and on the cases. The evaluator is the scalar form's. It also checks generated programs in the scalar form that no source produces, such as a jump that swaps two values, and a call with more arguments and results than registers to carry them, of a function that passes its own parameters on in another order |
| A target's executables have the same bytes whichever machine builds them | The SHA-256 of each case's executable for each target is kept in `bootstrap/tests/hashes` and compared on every kind of machine. `SMITH_UPDATE=1 cargo test cases`, the command that rewrites the cases' expected output, rewrites it too |

The tests whose result depends on the kind of machine they run on are one group: the last two properties, and what `smith run` and a build with no `--target` do. `cargo test -- --group=machine` runs only that group, and `cargo test -- --group=once` runs only the others.

Continuous integration runs for a pull request and in the merge queue, and for nothing else: the `timings` and `release` workflows are the only ones that run after a merge. The queue runs every check a pull request runs, and checks the messages of the commits it is about to merge.

- On Linux on x86-64 one job builds the compiler once and runs the format check, the lint, both test groups, and the timings. The machine group also runs once on each other kind of machine a target needs, when a change could alter what it finds there, as below.
- **A machine job runs only when its executables changed.** The machine group builds programs and runs them, so its result for a target is fixed by the executables it would run for that target, and what each is checked against, which is what the evaluator says or the case's `.out` file holds. `cargo test -- --fingerprint` runs nothing and prints, for each target, one hash of all of them, as the tests that depend on the machine would run them: the generated programs, the cases, and the hashes kept in `bootstrap/tests/hashes`. Every such test gets its executables from `targets::build`, which is how a fingerprint sees each one, so a new test cannot run an executable the fingerprint misses. A job named `decide` builds the compiler of the commit the change is based on and of the change, on Linux on x86-64, which is never scarce, and the two builds run side by side. It reads both fingerprints, and `.github/needs-machines.sh` names the machines, listed in `.github/machines`, whose target's hash differs. Only those machines run their job, and the others count as skipped on purpose, so `ci` passes.
- A change to the generators or the tests changes which programs are built, so it changes the fingerprint, and one byte in one generated program changes the hash of its target and of no other. The fingerprint is made without running anything, so it is the same on every machine, and it takes less time than a machine job, which builds the same compiler and runs what the fingerprint only builds. When the commit the change is based on cannot make a fingerprint, or the change cannot, every machine runs.
- This loses no checking. The commit a change is based on is on `main`, and every commit on `main` either ran each machine job and passed, or was skipped there because its executables were those of the commit before it, so the executables a machine would run on the base all passed there. A machine that would run the same executables, checked against the same results, can only pass again. A machine that would run any other executable runs its job. The machine tests check executables, so a change that alters none has nothing for them to find. How `smith run` starts a program on a kind of machine is checked again by the next change that alters that machine's executables.
- A change that touches no file under `bootstrap/` or `.github/` and not `.gitattributes`, which sets the bytes of the test cases on checkout, or `install.sh`, which the tests run, such as one to `knowledge/` or `.agents/` alone, runs no job that builds the compiler. `.github/changed.sh` decides this.
- A job that builds nothing, `.github/check-status.sh`, runs for every change and fails when a page of `language/` other than `index.md` has no `status`, a `status` other than `built`, `partial`, or `planned`, or `partial` without `built`.
- A newer push to a pull request cancels the run for the older push.
- A machine may not be available. GitHub ends a job that waits about fifteen minutes for one as `cancelled`, with no step run, and nothing in the workflows sets that limit, so a job that never started is not a failure of the change. Each machine job that never started is started again, and again a second time, in the same run, and only then counts as failed. `.github/never-started.sh` decides this from the list of jobs and how each ended, so it is tested without GitHub. Whether to look for such a job never depends on the result GitHub gives the matrix of machine jobs, only on the run not having been cancelled. `ci` judges each kind of machine by its newest job, so the merge queue never removes a pull request for a wait, while a job that ran and failed, or was stopped while running, is never started again. It passes only when it has read the list of jobs and found, for every kind of machine the change needs, a newest job that succeeded: a list that could not be read, an empty one, a machine with no job, a job that names no machine, and a skipped place standing for a lost job all fail.
- The job named `ci` is the one check the repository requires. `.github/ci-result.sh` makes it succeed only when every other job succeeded or was skipped on purpose, and fail when any failed or was cancelled. Adding a job to the workflow means adding it to what `ci` needs, and changes no rule.

## Speed

Smith's scalar code is meant to be as fast as C's. A set of programs is each written in Smith and in C, the C built at its compiler's highest level of optimization, and the run time of both is recorded with the [timings](#timings) after every merge. A pass or a change to a target is judged by what it does to those numbers.

## Timings

The time each step takes is measured from the first program, so a slowdown is seen in the change that causes it.

- `smith build --timings`, and `smith run --timings` before the program runs, print to standard error one line for each stage in the order they run, then a line for the total. The stages are `lex`, `parse`, `check`, `inline`, `fold`, `common`, `remove`, `emit`, and `write`, which saves the executable. A build that fails lists the stages that ran, after its errors. The executable is the same with the flag and without it. `smith check --timings` prints the same, after what the check reports, and as nothing is built its stages end at `check`.
- A test run ends with its slowest tests and how long each spent computing.
- Continuous integration prints both on every run.
- After every merge to `main`, the `timings` workflow adds one line to `history.csv` on the `timings` branch and changes no earlier line. Pull requests record nothing. The job is the only one that can write to the repository, and it writes only to that branch. `.github/record-timings.sh` makes the line, and `--header` prints the first line.
- The first line names the fields: `commit`, `date`, `compiler_build_ms`, `compiler_bytes` (the optimized executable), `test_ms`, `job_ms`, and `programs`. `programs` holds space-separated `<case>/<target>/<field>=<value>` entries, for each file in `bootstrap/tests/cases` that builds and each target the job's machine can build for. The fields are the five stages and `total` as `<stage>_ms`, and `bytes`, and `run_ms` only for the target the machine can run. The entries grow with the cases and targets, so the first line stays the same.
- To trace a change for the worse, read `history.csv` on the `timings` branch, find the first line where a field jumped, and take its `commit`.

## Releases

Every merge to `main` that changes what the compiler does publishes a release on GitHub.

- The version comes from the subjects of the commits since the last release, and from nothing else. `.github/next-version.sh` works it out and can be run by hand. While the first number is 0, a commit marked breaking with `!`, or a `feat`, raises the second number and sets the third to 0, and otherwise a `fix` or a `perf` raises the third. The first release is `v0.1.0`.
- A merge is released only when one of its commits is a `feat`, a `fix`, or a `perf`. Merges of `docs`, `test`, `ci`, `refactor`, `build`, `chore`, and `revert` commits publish nothing.
- A release holds the optimized compiler for each of the six kinds of machine a target needs, named for its target, as in `smith-linux_x64` and `smith-windows_x64.exe`, and `SHA256SUMS`, the SHA-256 hash of each. Its notes list the subject of each commit since the last release.
- `smith --version` prints `smith` and the version the compiler was released as. A build that is not a release prints `smith dev`.
- After every merge to `main`, the `release` workflow works out the version, builds on each kind of machine, and publishes. Pull requests and the merge queue never run it, and nothing waits on it. Only the job that publishes can write to the repository. Running it again for a commit already in a release makes no second one and changes none.
- `install.sh`, at the root of the repository, installs the newest release on macOS or Linux with `curl -fsSL https://raw.githubusercontent.com/smith-ai-lab/smith/main/install.sh | sh`. It takes the file for the machine from the newest release, checks it against that release's `SHA256SUMS`, and only then puts it in `~/.local/bin/smith`, replacing any earlier one. A machine no release file is for, a hash that does not match, and a download that fails each install nothing, leave an installed `smith` as it was, and exit with a non-zero status. It holds no version and no hash, so no release changes it, and it needs only `sh` and `curl` besides what macOS and Linux hold. When `~/.local/bin` is not on the `PATH` it ends by saying so and giving the line to add. The whole script is one group at the start of the file, so a script cut off part way, as when the connection drops, runs nothing and exits with a non-zero status. The tests run it against a release on disk. One further check, `.github/check-install.sh`, installs from the real newest release on each kind of macOS and Linux machine CI uses, and runs only when `install.sh` or that check changes.
- A machine may not be available. GitHub ends a build that waits about fifteen minutes for one as `cancelled`, with no step run, and nothing in the workflow sets that limit. The build of a target that never started is started again, and again a second time, in the same run, and the release is published only when the list of jobs was read and the newest build of every target succeeded. A build that ran and failed is not started again, and no release is made from fewer than six. A run that works out a version and does not publish it, because a build failed or for any other reason, is a failed run and says so; a run that succeeds has either published or had nothing to publish. A job that follows one that may be skipped on purpose has a condition of its own that says when it runs, because without one it is skipped whenever any job before it is. `.github/never-started.sh` decides this, as for [Testing](#testing). The same holds for when to look.
- Every release is kept. None is deleted or replaced.
- Once Smith compiles itself, each release is built by the release before it, and then by itself. The two results are the same byte for byte, as in [Bootstrap](#bootstrap).

## Mirror

Everything git holds is also on GitLab, at `gitlab.com/smith-ai-lab/smith`, so that it survives if GitHub is unavailable.

- It holds `main`, the `timings` branch, and every `v*` tag under its own name, each at the same commit as on GitHub. It does not hold release files, issues, or pull requests, which git does not hold.
- The `mirror` workflow runs after a push to `main` or a `v*` tag, and after the `timings` and `release` workflows finish, since a push made with a workflow's own token starts no other workflow. It can also be started by hand. Pull requests and the merge queue never run it, and nothing waits on it.
- It never forces. If GitLab holds something GitHub does not, or the push fails for any reason, such as a key GitLab refuses, the workflow fails and says why.
- It signs in with the private key in the secret `GITLAB_MIRROR_KEY`, a deploy key of that one GitLab project, and trusts GitLab only by the host key in `.github/gitlab-known-hosts`. Only the job that pushes reads the secret, and it cannot write to this repository.

Every file of every release is also on GitLab, in the package registry of `gitlab.com/smith-ai-lab/smith`, so that the compiler can be fetched when GitHub is unavailable.

- The files of the release `v0.5.0` are the package `smith` at version `0.5.0`, and anyone can download one without signing in, as in `https://gitlab.com/api/v4/projects/smith-ai-lab%2Fsmith/packages/generic/smith/0.5.0/smith-linux_x64`. `SHA256SUMS` is among them, and each file has the hash GitHub's `SHA256SUMS` gives it.
- The `mirror-releases` workflow runs after the `release` workflow finishes, and can be started by hand, which copies every file of every release that GitLab lacks. Pull requests and the merge queue never run it, and nothing waits on it.
- A file already on GitLab is never uploaded again or changed. If its hash differs from GitHub's, the workflow fails and names the file. Which files to upload is decided by `.github/plan-uploads.sh` from two lists alone, so it is tested without GitLab or GitHub.
- Both lists are read whole, however many releases and files there are, and a list that cannot be read fails the run before anything is uploaded.
- Either list may be empty: an empty GitLab gets every file. When the run ends in success, GitLab held every file GitHub holds with the same hash, because the script reads GitLab's list again after uploading and fails, naming a file, when one is missing or differs.
- It signs in with the secret `GITLAB_PACKAGE_TOKEN`, a deploy token that may write to the project's package registry and nothing else. Only the job that copies reads it.

## Bootstrap

The first compiler is written in Rust with no dependencies. It is retired once Smith compiles itself, and until then its code is the measure: the port must match it byte for byte, so it is made fast before the port and not after.

- **It implements only enough Smith to write a compiler:** functions, structs, enums, `match`, generics and traits, strings, lists, maps, files, arenas, `with`, and `try`, followed by contracts and the `arith` procedure. Arrays with `for`, differentiation, devices, and tasks are built afterwards, in Smith.
- **It grows one feature at a time, end to end.** Each step takes a feature from source text to a running executable on each target built so far, starting with a program that prints one line. A program that uses a feature not built yet gets an ordinary error, with a line and column, that says so. The test generators cover what the compiler accepts so far and grow with it.
- **A trait a type is given automatically has no source.** The `Fmt`, the `Repr`, and the `Eq` a type is given are each a function the compiler writes for each type a call from `main` needs it for, and `Repr` and `Eq` call those of each part. Nothing is written for a type whose given traits are never called.
- **Type parameters and lambdas are gone before the target seam.** A function with type parameters is checked once, with each parameter standing for any type that meets its bound. It is lowered once for each choice of types that a call from `main` makes, and a call of a trait's function becomes a call of the implementation its types choose. A function that takes a function is lowered in place at each call, and a call of its parameter becomes the body of the lambda it was given, or a call of the function with a name it was given. The scalar form holds no type parameter, no trait, no lambda, and no value of a function type.
- **A choice is a `match`.** `if`, `not`, `and`, and `or` are read as a `match` on a `bool`, with a block for `.true` and one for `.false`, so the right side of `and` and `or` is in the block that runs it, and the scalar form chooses only by a number.
- **A `match` on whole numbers is one `Switch`.** The number of the first literal the value equals, or the number of literals when it equals none, is worked out with a comparison and a choice for each literal, and the one `Switch` that follows goes to the block of that literal, however many there are. A `match` on text tries its literals in order, each by comparing bytes, and a pattern inside a case or a tuple is taken apart after the case or the tuple.
- **A `loop` is a block that jumps to itself.** Each round is one block, which takes the number it is on, when the loop runs over a range, and the value of every name its body assigns, and goes to the next round, or leaves. A `break` leaves too. The blocks that leave meet in one block, which takes the names' values and is where the code after the loop continues. A loop over a range compares the number with the end, as unsigned or signed numbers by its type, and adds 1 to it, which cannot overflow because it is below the end. An open-ended loop has no number and no comparison.
- **Its machine code is plain.** Every value a function holds lives in a register, or in its frame on the stack when none is free, and a call passes its arguments and results in registers, as [Scalar form](#scalar-form) says, and only a call into the system follows the system's convention. Only a call into the system follows the system's convention.
  - **Every target is built from the [scalar form](#scalar-form)** and reads nothing else, so it has code for each instruction of the form and each way a block ends, and none for a feature of the language. A value is held at the size of its type, 1, 2, 4, or 8 bytes. In a register it has zeros above its size, and in the frame it is at a place that is a multiple of that size, read and written at that width. One allocator, as [Scalar form](#scalar-form) says, gives each value its home. A frame holds, from the bottom, the bytes the system may use, room for the largest call the function makes, the values that have no register, each at the next multiple of its size, and the registers the function must put back. On Windows, which gives a program its stack one page at a time, a function whose frame is larger than a page of 4096 bytes first touches each page below the stack pointer in turn, from the nearest, then moves the stack pointer and touches the lowest page, so that no page is passed over. A frame of at most a page is reserved in one step, and on Linux and macOS every frame is. A function's own parameters arrive in the registers of its call, and in its caller's frame when there are more than registers, and it moves each to its home when it starts, as if all were copied at once, which copies nothing for a parameter whose home is the register it arrives in. A call puts the arguments that do not fit in registers at the bottom of the caller's frame, each at its size, and the function puts the results that do not fit after them. Its results go back as its arguments came. A jump gives the block its parameters as if all were copied at once, so a jump that swaps two values swaps them, between registers and places in the frame alike, and a block that is next in the function is not jumped to. The three targets of a chip differ in code only in the three calls into the system the form has, to write, to ask for memory, and to end, and in the file each writes.
- **Every program is lowered to the [scalar form](#scalar-form).** It is lowered from the checked program, where every value's type is known, and every target is built from it. It has a check that every function is well formed, and an evaluator that gives what the program prints and the status it ends with. A `bool` is one byte, and a number of an integer type is one integer of its type's size, 8 to 64 bits. The form has the instructions this needs: arithmetic and comparison at each width, widening and narrowing, a choice between two values, a read and a write through an address, each of one integer or one address at its size, and a call into the system that asks for memory. An address is moved by adding a number of bytes to it, and an instruction names no offset. The check refuses a read or a write whose address is not an address, and one whose value has another type than the instruction says.
- **The first four passes over the scalar form are built.** They are inlining, folding, working out the same thing once, and removal, as [Scalar form](#scalar-form) says. Each takes a program in the scalar form to a program in it, and the targets are given what the last gives.
- **What a program needs at run time is written once, in the scalar form,** and no target has a copy of it: joining texts, writing a text as a string literal, writing an integer of each type as its digits, and saying whether two texts have the same bytes. Each is a function whose loops are blocks, and a program holds only those it reaches. A text's bytes are read and written as 8-bit values. The evaluator can be given a limit on the memory the system will give, and when a request is refused it ends with status 1, having printed the start of what the program prints with no limit.
- **`arith` is built for a plain `+`, `-`, and `*` on whole numbers.** The checker asks it, for each operation, whether the result fits the type of the operands, and a result it cannot prove is an error that states the goal, such as `i + 1 <= 255`. It is given the range of each variable's type, the value of each literal, what each binding that is not a `var` was given, and the condition of each `if` the operation is inside, which holds in the first block and is false in the `else`, with `and`, `or`, and `not`, and, inside a `loop i in a..b`, that `a <= i` and `i < b`. A `var` is known only by the range of its type, inside a `loop` that assigns it and after. A product of two values that are not constants is one variable, which only a fact about the same product bounds. `arith` decides linear arithmetic over the integers exactly, by the Omega test, so the time it takes depends on the facts and the goal and never on the ranges of the types. The scalar form of an accepted operation is the one instruction.
- **A list is three values in the scalar form:** the address of its elements, its size, and how many elements the memory at that address has room for. An element is held as its scalars, one after another, each at a place that is a multiple of its size, and takes a whole number of its largest scalar, so that a `u8`, a text, and a struct of a `u8`, a `u32`, and a `u8` take 1, 16, and 12 bytes. The elements lie one after another, each at that size. Reading `xs[i].field` reads only the scalars of that field, and `xs[i].size` only the size. `push` writes the new element after the last and gives the list at the same address when there is room and the list is a `var` that is assigned the result. Nothing else can write there: a list that is lent is never returned, a name that is not a `var` never grows, and a `var` that is given a list that no literal, `[]`, or `push` just made, such as another name's, has no room beyond its size, so its next `push` copies. A list held inside another value, a struct, a tuple, an enum, or a list, has no room beyond its size for a `push`, which copies it, because only a `var` that a literal, `[]`, or `push` just made a list for grows in place. In every other case `push` copies the elements into memory with room for twice its size, or for one element when it has none. The memory is asked of the system as a text's is, and nothing is given back until arenas are built.
- **The standard library starts inside the compiler.** Until the bootstrap compiler can compile the library's own source, it supplies what a program uses from it, beginning with `print` and the `console` effect. `Option`, `Result`, `bool`, `Order`, the `Fmt` trait with its implementation for `str`, the `Repr`, `Eq`, and `Ord` traits, the `Wrapping`, `Saturating`, and `Checked` traits, the implementations of those for every integer type, which call functions the compiler builds and only the prelude may call, and the `Try` trait with its implementations for `Result` and `Option` are Smith source that the compiler reads with every file, and a program reaches them only through its own code, so they cost a program that never uses them nothing.
- **The words a program is started with are given only to a `main` that takes them.** A `main` with no parameter is compiled as it always was, and its executable asks the system for nothing about how it was started. A `main` with the parameter `args: List[str]` is called by a function of the runtime, written once in the scalar form, whose three parameters are what the system gives, the same on every target: a number, the address of that many addresses of texts, and the address of a command line. Linux and macOS give the number and the addresses, as the kernel and the loader start a program, and Windows gives the command line with the number all ones, from `GetCommandLineW`, another thin wrapper over the kernel. The runtime makes the list of the words after the program's name. For the system's words each is the system's own text, kept where it is, and for Windows the command line is split by the rules of the C runtime of Windows, with its units of UTF-16 written as UTF-8, so every target gives the same words. Memory is asked of the system only when there is a word. Each word is checked to be valid UTF-8 before `main` runs, by a function of the runtime that, for the first that is not, writes `argument N is not valid UTF-8` and a line break to standard error, where N counts from 1, and ends the program with status 1. The scalar form has a call into the system that ends the program with a failing status, and a stream for standard error.
- **Text built at run time lives in memory asked of the system.** Each string that holds an expression, and each integer written as its digits, asks, through the boundary [Standing alone](#standing-alone) names, for exactly the bytes of the text it builds: `mmap` on Linux and macOS, and `VirtualAlloc` on Windows. Nothing is given back while the program runs, so all of it belongs to `main` and is freed when the program ends. A request the system refuses ends the program with a failing status, as [Tasks and async](language/tasks.md) says of running out of memory. A program that builds no text at run time never asks, and its executable does not import the call.
- **Types depend on numbers first.** The bootstrap compiler lets a type mention a `Nat` and checks statements of arithmetic with `arith`. Statements about anything else, and proofs written out as functions, are built afterwards, in Smith. What the bootstrap compiler accepts is a part of that larger system and never a different one.
- **Contracts come before the port,** so that the compiler's own source, which is full of integer arithmetic, is the first real test of arithmetic that must be proven to fit.
- **The port is done by hand and checked by comparison.** The compiler is rewritten in Smith. For every program a generator produces, the Rust compiler and the Smith compiler must emit identical bytes.
- **The switch.** The Rust compiler builds the Smith compiler, that builds itself, and that builds itself again. The last two binaries must match byte for byte. Then the Rust compiler is deleted.
