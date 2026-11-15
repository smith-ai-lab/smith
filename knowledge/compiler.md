---
type: specification
title: The Smith compiler
description: How the compiler is designed. It covers how for expressions and effects are compiled, the compiler's structure and commands, its representation, backends, what an executable depends on, testing, and bootstrap.
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

## Scalar code

The compiler's own code and the edges of a server are scalar. They get a fixed list of roughly linear-time passes: inlining small functions, constant folding, dead-code and common-subexpression elimination, instruction selection, and register allocation. Nothing iterates to a fixpoint, and value semantics means no alias analysis is needed. New passes are added one at a time, each proven or validated.

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

- Checking a body needs only the signatures of what it calls. Running it needs those bodies too, all the way down.
- Code that no root reaches is not compiled, so it can hold an error while a single test runs. That lets a new design be tried with one test before every caller is updated. The error surfaces when a command whose roots reach it is run.
- `grad(f)` needs the body of `f` and of everything it calls. Its type depends only on the signature of `f`, so checking stays local, and whether `f` can be differentiated is decided when the code is built.

### Errors and warnings

An error stops every command. A warning is reported only by `smith check`, which fails if there are any, so it is the lint step and a continuous integration run needs no extra flag.

An error names its file, line, and column, as in `hello.smith:2:3: message`. Lines and columns count from 1, and a column counts characters.

- Dead code, meaning a definition no root reaches, is a warning.
- An unused variable or an unused import is a warning.
- An unused value whose type cannot be discarded is an error, as [Ownership](language/ownership.md) requires.

### Commands that report

The command line is the compiler's interface for people and for agents. Every command can print its result as JSON.

| Command | Reports |
| --- | --- |
| `smith check` | Errors with a counterexample, the remaining proof goal, and candidate fixes, plus warnings |
| `smith hole` | The type, effects, and proof goal a `hole(name)` needs |
| `smith find` | Definitions matching a type signature, so existing code gets reused |
| `smith deps` | What a definition uses, and what uses it |
| `smith cost` | Allocation and operation counts as formulas over the sizes, and which arrays were stored |

## Representation

The pipeline is the classic one: tokenize, parse, collect signatures, check, lower, and emit. A run that only checks stops after the fourth step.

The lexer and parser are hand-written: recursive descent for declarations and a Pratt parser for expressions. One syntax tree serves the checker and the formatter. Every node keeps its source span, comments are ordinary nodes attached to what follows them, and an error node holds any tokens the parser skipped, so broken programs and holes are still representable.

| Level | What happens there |
| --- | --- |
| Surface tree | Parsing and formatting |
| Typed core term | Checking and `grad`. About ten node kinds |
| Kernel form | Fusion, contractions, and vectorization. Shared by the CPU backend and both GPU emitters |
| Machine form | Instruction selection and register allocation |

## Backends

| Target | Output |
| --- | --- |
| CPU | arm64 and x86-64 machine code, written to Mach-O, ELF, and PE directly |
| Apple GPU | Metal shading language source, compiled by the OS at load time |
| Vulkan GPU | SPIR-V binary |

Everything up to the kernel form is the same for every target. Each CPU target supplies its instruction selection, its calling convention, and its executable writer, and that seam exists from the first program. The compiler writes executables itself and runs no linker or assembler.

The first two targets are built together. A target has one name, which is its case in the `Target` enum that `build` takes in `project.smith`, and the same word selects it on the command line, as in `--target linux_x64`.

- **Linux on x86-64,** named `linux_x64`. A static ELF executable that talks to the kernel directly.
- **macOS on arm64,** named `macos_arm64`. A Mach-O executable that reaches the kernel through the system library, which it links dynamically, and that carries the ad hoc code signature Apple silicon requires.

Everything runs as native code, including compile-time code such as literal conversion. The checker contains a small symbolic evaluator that simplifies expressions with unknowns, such as `n + 0`, to compare types. It defines what core terms mean, so it is also the test oracle.

## Standing alone

A Smith executable carries everything it runs. All of its code is compiled from Smith source into the one file, in the same way that outside code is [vendored](language/projects.md) into the project and never fetched from a shared cache. The only thing the executable needs from the machine is the operating system's own stable boundary, and that boundary is the kernel wherever the kernel allows it.

| System | Boundary | Why |
| --- | --- | --- |
| Linux | The kernel's system calls, made directly | The kernel keeps them stable, so the executable is fully static and uses no C library |
| macOS, iOS | `libSystem`, loaded by the system's dynamic loader | Apple changes system call numbers between releases, and Apple silicon does not run a static executable |
| Windows | The system DLLs | Windows changes system call numbers between builds |

- From a system library a program calls only the thin wrappers over the kernel, such as `write`, `mmap`, and `exit`. It never uses the C library behind them. Allocation, formatting, and everything else are written in Smith and behave the same on every target.
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
| Self-hosting | The compiler built by itself twice is byte-identical |

- Each property is a theorem to prove later, so it is written in that form now.
- Specific cases live as `.smith` files with expected output beside them, updated by one command and reviewed as a diff.
- Tests run in-process and in parallel.

## Bootstrap

The first compiler is written in Rust with no dependencies. It is kept small and retired early.

- **It implements only enough Smith to write a compiler:** functions, structs, enums, `match`, generics and traits, strings, lists, maps, files, arenas, `with`, and `try`, followed by contracts and the `arith` procedure. Arrays with `for`, differentiation, devices, and tasks are built afterwards, in Smith.
- **It grows one feature at a time, end to end.** Each step takes a feature from source text to a running executable on each target built so far, starting with a program that prints one line. A program that uses a feature not built yet gets an ordinary error, with a line and column, that says so. The test generators cover what the compiler accepts so far and grow with it.
- **The standard library starts inside the compiler.** Until the bootstrap compiler can compile the library's own source, it supplies what a program uses from it, beginning with `print` and the `console` effect.
- **Contracts come before the port,** so that the compiler's own source, which is full of integer arithmetic, is the first real test of arithmetic that must be proven to fit.
- **The port is done by hand and checked by comparison.** The compiler is rewritten in Smith. For every program a generator produces, the Rust compiler and the Smith compiler must emit identical bytes.
- **The switch.** The Rust compiler builds the Smith compiler, that builds itself, and that builds itself again. The last two binaries must match byte for byte. Then the Rust compiler is deleted.
