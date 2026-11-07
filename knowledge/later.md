# Later

Ideas that are not on the roadmap of #73, one line each with the closed issue that held them (owner, 2026-11-07: "350 is insane. We're never going to be able to do all that, and it seems highly speculative ... only keep stuff that we like really need and know we need"). Nothing here is worked on. When the lead and the owner move an idea up, it becomes a new ticket written fresh from the code as it is then ([smith-dev-tickets](../.agents/skills/smith-dev-tickets/SKILL.md)); the old issue is history, not a spec.

## Editor and formatting (roadmap step 3, zero patches)
- #2048: smith lsp: format-on-save for .smith through front::format, never clang-format
- #1952: lsp: smith's clang-tidy checks and fallback config for .c without patch (part of #1887)
- #2049: agents format .smith files as they write them: a Claude Code hook in smith new's settings and in smith's repo
- #2050: smith new: a committed pre-commit hook that formats staged .smith files (`smith fmt --staged`)
- #2047: test: hello world in the new syntax from a fresh `smith new` project (acceptance test of the switch-over)

## Docs: the plugin knowledge base
- #2026: plugin: the language as a knowledge graph in plugin/knowledge, one concept per idea, new syntax only (part of #1631)
- #2059: plugin knowledge: concept pages for values, types, control flow and loops (piece 3 of #2026)
- #2060: plugin knowledge: concept pages for data, functions, lambdas and methods (piece 3 of #2026)
- #2061: plugin knowledge: concept pages for generics, compile time, attributes and privacy (piece 3 of #2026)
- #2062: plugin knowledge: concept pages for modules, C interop and libraries (piece 3 of #2026)
- #2063: docs-gap label and its line in the runner friction step (piece 4 of #2026)
- #1802: test: every example in the new-syntax tour parses with the new front end
- #2055: plugin knowledge: gap in the tour, contracts and smith prove, in the new syntax (piece 2 of #2026)
- #2056: plugin knowledge: gap in the tour, autodiff, in the new syntax (piece 2 of #2026)
- #2057: plugin knowledge: gap in the tour, effects (IR pass, ceilings, function pointers, generics), in the new syntax (piece 2 of #2026)
- #2058: plugin knowledge: gap in the tour, memory, in the new syntax (piece 2 of #2026)

## Language features from the new-syntax spec (#1428) that porting does not need
- #1827: front: `for x in c` over a container with `pub fn items(c: *C) -> iter(E)`, and `for x in it` over an `iter(T)` value (#1428 revision)
- #1828: front: generators lower to state machines, not fibers; `stream` is renamed to the built-in `iter(T)` (#1428 revision)
- #1829: std.seq: range, enumerate, zip, refs, take, drop, peekable as generators
- #2513: front: sizes in brackets (`n: usize`) are inferred from argument shapes, and a sized array passes for `[,]T`
- #2514: front: `==` compares values for every type, slices included
- #2515: std.array: `zeros`, `ones`, an identity matrix and `filled_like` over `array.filled`
- #2516: front: check index comprehensions: infer each index's range and require equal sizes where an index is shared
- #2517: front: emit index comprehensions as fused loops that Enzyme differentiates
- #2528: front: a run-time size mismatch between arrays traps with a message naming the size and both values
- #2518: lsp: hover shows an index's inferred range and a call's inferred sizes
- #1822: front: `methods(T)` reflection, the functions of T's module that take T first (#1428 revision)
- #1836: front: user-defined concepts as predicates, with `requires` (#1428 revision)
- #2539: std.mem: `mem.alloc(T, n | [a, b])` becomes an ordinary generic of mem.smith; the front end built-in is deleted
- #2558: lib: remove `mem.equal`; its callers write `a == b` (rest of #2540)
- #321: lang: bf16, and vector types with swizzles (f32x4, v.xyz) on the CPU and the GPU
- #2633: Tags you define yourself for backtick literals: html`...`, css`...`, sql`...`, with holes `${x}`
- #2537: front: import C++ headers into .smith (build #361's design: declarations, generated wrappers, calls, templates, std.cpp, editor, first shim)
- #1787: syntax: struct fields are private unless pub
- #1823: std.array: a NumPy-like array library, design of the first slice (filled, shape, indexing, elementwise ops, reductions)

## Other
- #1054: contract: decreases, a measure the prover checks on every recursive call
- #1749: build: frontend-coverage leaves out fixtures that test an error, and keeps a history
- #1841: CI: a compile-time budget on the test beds (clean build and one-file rebuild)
- #2045: tests: tree-wide parse of the fixed tree in tree_sitter_test, no ERROR node (rest of #1994)
- #2465: language: every `[]mut T` parameter is non-overlapping by default (replaces C's restrict), checked by smith prove at each call
- #2538: smith's runtime C becomes .smith modules in the new syntax: build runtime, executor, make reader, mcp, verbs, packages, smith.c

## Concurrency, async and effects
- #239: Effect handlers: the interposition runtime, per-task handler tables, mem::Allocator, checked and failing
- #241: Task runtime: M:N tasks on one worker per core, kqueue I/O that parks a task, the lock rule, pin_main
- #324: Failure: let it crash. A checked failure fails its task, unwinds through defer, and propagates to the group
- #396: effects: inline assembly parsed for its effects; function pointer types carry effects; loaded code checked against them
- #397: effects: prebuilt libraries get effect summaries by disassembly at link time
- #436: Safe concurrency in modules: groups, task handles, generators with yield and streams; no data races and no deadlocks, by construction
- #437: std::parallel: for_each and reduce over owned data, race-free by construction
- #438: Linear types replace affine: used exactly once, _ = x discards, a forgotten close is a compile error
- #439: Regions: every pointer belongs to an inferred region; no dangling pointers, use after free or leaks; ownership idioms inferred, declared at the boundary
- #447: signal: signals arrive as a stream; installing a signal handler is an error in a module
- #501: console::fake, and print on every stream (files::print, net::print), destination first
- #504: A Writer every stream gives, and one print over it (after #396: effects on function pointers)
- #556: smith::thread: start, join, detach and a lock for .smith (lsp needs them)
- #572: Linear values need a borrow: use a value without consuming it
- #725: Ownership idioms inferred from a function's body and summarized across functions, like effects
- #726: Function pointer types carry an ownership bound; prebuilt libraries declare idioms on their declarations
- #945: task: tasks, typed task::spawn and task::join, skill smith-task
- #946: task: tasks, the 1,000-connection echo benchmark
- #947: task: tasks, io_uring for files and sockets on Linux (epoll and the file pool as the fallback)
- #959: task: lambdas, [[smith::until(g)]] and the written-after-creation rule
- #1020: task: effects API, the error table and the handle table
- #1021: task: effects API, the allocator interface
- #1022: task: effects API, the console, env, clock and entropy interfaces
- #1023: task: effects API, the files interface
- #1024: task: effects API, the net interface
- #1025: task: effects API, the process interface
- #1026: task: effects API, the scheduler interface
- #1027: task: effects API, `K::use` and `K::with` on every effect
- #1028: task: effects API, the Reader primitive
- #1029: task: effects API, the Writer library
- #1030: task: effects API, split the handlers runtime into one file per kind
- #1033: task: effects API, scratch: a second allocator effect, always an arena
- #1037: task: streams, task::spawn on a generator, the bounded buffer and closing at the group's end
- #1039: task: streams, stream::merge
- #1040: task: sync, sync::Mutex and sync::with
- #1041: task: sync, sync::Atomic
- #1048: task: concurrency rules, the exhaustive small-scope deadlock test
- #1962: std.mem: composable allocators (Alexandrescu's design), the default built from primitives, debug as Checked around it

## Standard library and libc
- #33: <smith/math.h>: the elementary functions with the same bits everywhere
- #199: smith::slice and smith::ascii replace text; format::parse returns a result
- #327: Library errors follow the panic line: misuse and exhaustion panic, what a caller can handle returns a value
- #449: List(T): a growable array through the current allocator; grows in place in an arena, a push ends borrows
- #498: slice::reshape: the shape of a multi-dimensional view (design first, split from #199)
- #500: The library's shape: pure and effect layers, one job per module, checked by effects (audit first)
- #557: smith::files: pipe, duplicate, redirect and a wait for input (lsp needs them)
- #558: smith::files and process: working folder, real path, modification time, process id (lsp needs them)
- #559: smith::process: a child in a folder, with added environment, quiet streams and a priority (lsp needs it)
- #920: libc gap: lseek, pwrite, ftruncate, fchmod, flock have no smith equivalent
- #923: libc gap: getpid, getppid, getuid, setpgid, getrlimit, setrlimit, prctl have no smith equivalent
- #924: libc gaps that need a design choice: exit and abort, signals, setjmp, dlopen, readiness waits, stdarg, raw sockets
- #1687: task: library errors, build runtime failures fail the task instead of exit(2) (part of #327)
- #1703: math: sin, cos and tan of our own with a stated ulp bound, plus wrap, clamp and lerp (part of #33)
- #1704: math: exp, tanh, sigmoid and softplus with a stated ulp bound (part of #33)
- #1705: slice: take, take_until, take_while and copy (part of #199)
- #1707: text: delete std::text once slice and format cover it (part of #199)
- #1711: std: slice.reshape with a compile-time count check (part of #199)
- #1765: math: f64 sin, cos and tan within 1 ulp by porting fdlibm's kernels and argument reduction (follow-up of #1703)
- #2436: std.process: wait for whichever child ends first, then use it in the frontend-coverage ring

## Tooling and infrastructure
- #291: No shell: the build and CI scripts become functions and tasks of the build file
- #419: lint: smith lint shows a project's .warnings as advisory output, never as a failure
- #535: Learn from data: record every smith run agents make, and rank the friction by what it costs
- #540: smith's logic out of C++: shrink shim/ to the LLVM edges, behind C interfaces, and write the rest in .smith
- #604: Audit fixed-size caps in smith's own code: none may skip silently
- #619: Narrow the lag rule: only smith's C is built by the previous release; its .smith code by this tree's smith
- #646: The testing pyramid: smith's core as a library, unit tests in process, a thin end-to-end layer
- #1457: task: port scripts/release.sh to the task smith build release (part of #291)
- #1657: task: fixed caps audit (#604), tools, archives and platform
- #1669: smith's own tooling is regular smith code: every C, shell and Python script ported to a module
- #1670: task: port scripts/bench.c, ci_times.c, revert_check.c to ci/ modules (part of #1669)
- #1673: task: port scripts/llvm_pack.c, llvm_prune.c, pack.c, pack.h to llvm/ and release/ modules (part of #1669)
- #1681: mirror: main, the tags and the weekly releases are pushed to GitLab.com on every merge
- #1690: task: port scripts/llvm_linux.c and sha256.h to smith (the Linux LLVM recipe)
- #1727: llvm: the pack identity hashes the recipe's flags and pins, not its files' bytes
- #1945: llvm: one generous configuration, built once per LLVM version by a CI job per platform, published as the pack (#1377)
- #2071: upstream: prepare the compiler-rt and Enzyme patches as branches on the owner's forks (the owner opens the PRs)

## Web, CLI and apps
- #87: checkers in idiomatic Smith
- #88: game in idiomatic Smith
- #288: smith::cli: command-line options parsed into a struct by reflection, help from doc comments
- #320: smith::window: windows and input on a task, window::next parks until an event
- #331: smith::serial: serialization through reflection; JSON, TOML, YAML and binary formats share one walk
- #334: smith::http server: a constexpr router built up front, one task per connection, an arena per request, handler failures isolated
- #336: Later: a browser playground for smith (the compiler built to WebAssembly, the site's examples run in place)
- #340: smith::html: a builder that writes as it runs, typed attributes per element from the standard, escaped by default, .attrs as the one extension point
- #341: smith::htmx: htmx::attrs(.post, .target, .swap) as html attributes through .attrs, no JavaScript shipped
- #342: smith's website: a static site on GitHub Pages generated by smith::http, previewed by its server, built from the README, skills and concepts
- #502: smith::log: OpenTelemetry's levels, typed pieces to a handler, readable stderr by default
- #503: Telemetry: smith::metric, smith::span and the OpenTelemetry handlers (OTLP over HTTP)
- #1148: epic: a world-class CLI: one table drives help, typo suggestions, completions, --json and the reference page
- #1839: std.ecs: design of an archetype entity-component system (design first)

## GPU, ML and autodiff
- #18: .smith on the GPU: kernels as plain functions, three layers, CPU/Metal/CUDA/ROCm backends, graphics, textures, ray tracing
- #59: smith's own safety checks after Enzyme: UB checks and NaN traps in gradients
- #89: learner in idiomatic Smith, with gradient over structs
- #322: smith ML library: design in progress (ops, nn layers, optimizers, training) from the learner's building blocks
- #546: gpu: every SIMD-group and quad function, function constants and tuning attributes; the learner's row ops, recurrence, SSM glue and conv retired
- #547: gpu: f16/bf16 and simdgroup matrices; the learner's matmul and gemm retired
- #548: gpu: a kernel's backward from autodiff of its CPU compile, emitted to Metal
- #549: gpu: the learner's selective scan (SSM) forward and backward from .smith
- #550: gpu: the CPU backend, thread groups as tasks
- #551: gpu: layers 2 and 3, gpu::each and gpu::map
- #552: gpu: vertex and fragment functions, textures, samplers and ray tracing; the game's shaders retired
- #554: gpu: layer 1 kernels built and launched from .smith; the learner's elementwise and reduce kernels retired

## Proofs
- #66: smith prove speed: lazy flattening, cheaper inlining, libraries proved once
- #407: prove: proved checks, remaining gaps after #404
- #445: prove: a call with modifies through a pointer into a restrict parameter's member reports 'may be poison' without naming the condition
- #448: smith lint --trusted: a generated report of every trusted site and its count
- #1052: Proofs are ordinary smith functions: lemmas, induction, math types, a proof library (Lean-level, purely inside smith)
- #1056: lib: math::Int, Real, Set(T), Seq(T) as proof-only types, a compile error in running code
- #1057: prove: the solver encodings of the math types
- #1058: lib: the standard proof library, first slice, with odd_sum_is_square as a test
- #2475: checker: infer a declaration's type from its later uses (bidirectional / Hindley-Milner within a function), e.g. `xs := []`
- #2505: design (later): Dex-style index sets, an index typed by its range

## Objective-C, C++ and platforms
- #67: smith on Linux and Windows, and cross-compiling
- #222: smith emits a project as portable C: lowered modules plus the runtime, buildable by any C23 compiler for any target
- #318: Objective-C from .smith: import Apple's headers like C headers, objc:: calls, owned objects, strings
- #319: Objective-C classes from .smith: objc::class_define with objc::methods(P, Data) tables, and objc::block
- #636: objc::block takes a function and a context as two values; a closure should lower to a block
- #1949: linux: smith links statically against musl (one file, no glibc)
