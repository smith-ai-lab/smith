---
okf_version: "0.2"
types: convention, architecture, contract, design
---
# Knowledge

What smith is and the contracts its code keeps. Read only the concepts your task needs. Skills say how; AGENTS.md holds the rules.

## Architecture

- [Affine types, a plugin linked into smith](affine-types.md): [[smith::affine]] on a type makes each value used at most once, checked by one analysis in a clang plugin linked into smith that runs on every compile and in clangd with no flag.
- [Effect inference, a plugin linked into smith](effect-inference.md): Effects are inferred for every function a compile reads, from its body and its callees, and [[smith::pure]] and one attribute for each effect are ceilings checked by one more clang plugin linked into smith.
- [Enzyme is linked into smith's lld, and smith::autodiff is its interface](autodiff.md): An autodiff program links with full LTO and no passes, runs AddressSanitizer after Enzyme in debug and has no UBSan.
- [smith build and build.c](build-file.md): smith build runs a project's build.c, which writes a ninja manifest that smith's own executor runs, with everything explicit and no macros.
- [Contracts are C23 attributes and calls that smith reads](contracts.md): Contracts are calls of smith::contract that smith prove reads from the IR, are not a clang plugin and do not depend on -fbounds-safety.
- [smith dbg and smith dap](debugger.md): smith dbg and smith dap are lldb and lldb-dap from smith's LLVM with the system's debugserver, and the doc holds how they are built, their cost, and macOS permissions.
- [The embedded files and the programs folders](embedded-files-and-programs-folders.md): The files clang looks up beside itself are embedded in smith and written once to a runtime folder that smith never removes, and the programs it runs are clones of smith in a per-binary folder.
- [clang-format, clang-tidy and clangd, from smith's LLVM](format-lint-and-editor-tools.md): smith fmt, lint and lsp are the LLVM tools linked into smith, with smith's defaults where a project has no file, and .smith is C to clang-format.
- [smith::format is a type-checked formatter, a library of its own](format-library.md): Formatted text is a generic over its arguments with no format string, each type picks its rule at compile time, numbers go through smith::text, and C gets only the writer it is built from.
- [The linker is lld, in smith's process](linker.md): The driver finds a copy of smith named ld that runs lld's Mach-O linker inside smith, and the system's other tools stay the system's.
- [smith build llvm](llvm-build.md): smith build llvm builds LLVM 23.1.3 static and pinned into a cache that survives clean, with the libraries smith links.
- [The shims that read clang's AST are compiled with a precompiled header](precompiled-shim-headers.md): shim/pch.h holds the clang and LLVM headers four shims include, build.c compiles it once to build/<OS>-<arch>/shim.pch as a step of the graph, and those shims are compiled with it, which saves about a third of their compile.
- [smith mcp](mcp-server.md): smith mcp is the debugger as an MCP server over stdio, on the fake-able io and the generated protocol types, with eleven tools.
- [Neovim support is this repository](neovim.md): plugin/smith.lua, editors/neovim and tree-sitter/ give the .smith file type, highlighting, the language server, debugging and tests in Neovim.
- [One compiler, in smith's own process](one-compiler.md): smith ships upstream LLVM 23.1.3 linked into itself, compiles C, C++ and Objective-C in its own process, and picks other compilers only through environment variables.
- [The platform layer](platform-layer.md): What smith does differently on macOS and Linux lives in one file per platform, runtime/platform_macos.c and runtime/platform_linux.c, with the same functions, and this table says what differs.
- [smith prove checks LLVM IR with Alive2, Bitwuzla and Z3](prover.md): smith prove proves each function with a contract in a process of its own, assumes functions marked smith_trusted, and uses no list.
- [smith::slice and smith::ascii replace text's slice operations](slice-library.md): A slice library generic over the element does what text did for chars, with no allocation, option for a search that may find nothing, and a character test that reads no locale.
- [The .smith language](smith-language.md): A .smith file is C with modules, lowered to C as clang reads it by a token hook, with sized types, the rules, one command, and what the language does not do yet.

## Conventions

- [Adopting smith is incremental from both ends](adoption-path.md): From an existing C project use smith cc, translate the Makefile, rename files one at a time, and from a new project follow conventions and generate only the files you change.
- [Builds by convention, smith_app and smith_app_tests](build-conventions.md): A project that follows smith's conventions needs no build file, and smith_app and smith_app_tests say what the conventions do.
- [smith::files, the direct-call files module](files.md): Files and folders as direct calls returning result(T, files::Error), over the files handler of the thread, with an in-memory fake and memory from the current allocator; the first replacement of io::Io's files (issue #240).
- [console, env and clock are small modules called directly](direct-call-modules.md): smith::console, smith::env and smith::clock replace the console, args_env and clock parts of io::Io with typed functions called with no value threaded from main, over the console, env and clock handlers of the runtime, so a test fakes them.
- [smith::process, the direct-call child process module](process.md): Child processes as direct calls returning result(T, process::Error) (run, spawn, start with pipes, wait, kill), over a process handler of the thread, with a scripted fake and memory from the current allocator; the third replacement of io::Io's parts (issue #240).
- [smith::net, the direct-call TCP and TLS module](net.md): Sockets and TLS connections as direct calls returning result(T, net::Error) (connect, connect_tls, listen, accept, read, write), over a net handler of the thread, with an in-memory fake that scripts servers and pairs clients with listeners; the fourth replacement of io::Io's parts (issue #240).
- [The handlers runtime](handlers.md): One runtime every smith program links (a dylib with __interpose tuples on macOS, an object defining the functions on Linux) that sends malloc and its family to the allocator of the calling thread; no handler is libc's; built in steps (issue #239).
- [The checked allocator](checked-allocator.md): mem::checked wraps an allocator and records the stack of every block, so a leak names its allocation site and a double free, a free of a foreign block and a use after free are reported; the test runner sets one around each test (issue #239, step D2a).
- [smith's libraries are a modern standard library for C](libraries.md): smith is batteries included, so a project needs no third-party library; each library does one thing, uses standard types, defines no macros, and is compiled from source by the project through smith_library.
- [smith runs on macOS 15 and newer](deployment-target.md): smith's own binary is built with -mmacosx-version-min=15.0 whatever macOS builds it, a newer system function is called only where it exists, and programs smith builds for users keep the user's target.
- [The SDK is chosen through the environment](sdk-rule.md): On macOS smith sets CLANG_NO_DEFAULT_CONFIG and SDKROOT for the caller that has not, and never adds a flag.
- [smith's own code](smith-own-code.md): smith's own code is .smith modules and C23, libc only, with no malloc; C++ is only the edges to LLVM.
- [smith's top level is only verbs, with fixed meanings](verbs.md): The verbs cc, c++, build, new, fetch, cache, docs, version, fmt, lint, lsp, run, test, check, prove and dbg each have one fixed meaning read from what the project declares.

## Contracts

- [The build API](build-api.md): Every call of <smith/build.h> with its fields, defaults and the generated manifest.
- [smith-effects, a program touches the world only through capabilities](effects-check.md): A clang-tidy check decided function by function from the source: raw effects live only in a capability's module, and io::make is called only in main.
- [Ownership idioms, what a returned pointer means](ownership-idioms.md): A returned pointer follows one of C's four idioms, written as [[std::returns_owned(f)]], returns_borrowed(n) or returns_internal; smith accepts, validates and records them, and judges no call site yet.
- [A module has no warnings](module-diagnostics.md): The compile of a .smith file with a module line fails on a fixed baseline of diagnostics, each with a written way to say I meant it; heuristic ones are opt-in and advisory.
- [The idiom checks](idiom-checks.md): clang-tidy checks smith-sized-types, smith-module-library, smith-module, smith-bounded-loop, smith-c23 and smith-constexpr tell an agent what idiomatic Smith and C23 look like.
- [A proof is fast, and fast is CPU time](proof-budget.md): Each proof has a budget of about 10 s of CPU time charged as on 8 idle cores, so a busy machine slows a proof and never fails it.
- [A module's generated C header](smith-module-headers.md): A module's header includes exactly what its exports need, and C and C++ code calls a module through it.
- [smith-preprocessor, the check that a .smith file has no directive](smith-preprocessor-check.md): A clang-tidy check reports every directive, _Pragma and C-macro c:: name in a .smith file, each with its replacement.
- [Typed memory functions, and the raw ones are an error in a module](mem-typed-functions.md): smith::mem has copy, move, zero, fill, equal, bytes and view over slices, and the clang-tidy check smith-mem reports C's memcpy, memmove, memset and memcmp in a module.
- [No preprocessor, and the target's facts](smith-target-facts.md): A .smith file has no preprocessor, import target gives the facts of the target as literals, and if constexpr picks a branch before the parser.

## Designs

- [smith reads its archives itself](archives.md): Gzip, xz and tar are read by smith's own C, and extraction refuses any name or link that could leave the package.
- [The 23 cases that shaped smith build](build-cases.md): Each of 23 build cases in make, ninja, Zig 0.17.0 and a smith build.c, with how they were run and what was verified.
- [How smith build runs](build-execution.md): What smith build does from compiling build.c to running the manifest, and what it records in .smith.
- [Found by porting game, the checkers example, what was measured and the open decisions](build-history.md): What porting game and checkers changed in the build API, the 23 cases summarized, measurements, pitfalls and the open decisions.
- [Two layers of the build API](build-layers.md): The high-level smith_app calls and the low-level object and program calls are one API at two levels.
- [build.smith, the module smith::build, and --dump-config](build-smith-and-dump-config.md): build.smith is a build file in .smith over the module smith::build, and --dump-config writes the build file the conventions imply.
- [Tests, smith_tests and smith::test](build-tests.md): smith_tests links the test files with the libraries they import into a runner whose workers run the tests, with seeds, shrinking and isolation.
- [The caches and smith cache](caches.md): Modules and packages are cached under one rule, an entry unused for 31 days is removed, each root has a byte cap with least-recently-used eviction, and smith cache shows and cleans them.
- [Clang modules, smith_modules](clang-modules.md): Clang modules are implicit, one per header, opt-in, and cached once per machine.
- [The executor](executor.md): The executor reads the subset of ninja's language that smith writes, schedules and decides rebuilds itself, and keeps its state in .smith.
- [Failure reports are read from the call stack, and a test's signature says what it touches](failure-reports-and-tests.md): A failing check prints frame addresses that smith symbolize turns into the failing line, so checks and contracts are plain functions, and a test's parameters decide its io.
- [CI runs the full check on the owner's Mac](ci.md): The merge train's batches and main run smith check on a self-hosted runner (no lock, one clone per job); a pull request runs no CI, and a green push to main installs and releases smith.
- [smith test --changed runs the tests a change can reach](changed-tests.md): The tests to run for a diff are the ones the reverse dependency graph reaches from the changed files, no test declares anything, and a path the graph does not know runs everything.
- [Records select the tests a change reaches](coverage-selection.md): A test that ran under coverage keeps a content-hashed record of the functions it executed and its inputs; smith test --changed reruns a test whose record is stale, and falls back to the build graph for what no record names.
- [A host-wide pool of cores limits heavy jobs](slots.md): smith build, check and test draw core tokens from a pool of flock'd files, one for the first worker and one more for each further worker, a waiting CI job goes first, and a smith run by a smith rides on its parent's token.
- [A stage0 release per merge to main](release.md): Every green merge to main publishes the GitHub Release stage0-<commit> with smith's tarball and checksum, and old ones are pruned.
- [What loads a Mac that builds with smith, and what a test may not depend on](machine-load.md): Spotlight indexes only a non-hidden build/ folder, Gatekeeper assesses each new executable once, and a test's limits must count CPU time, never wall time.
- [An import's functions run at compile time from the module's body file](body-files.md): A module's own compile writes its lowered functions to a .body file beside its header, and an importer that mentions one where C requires a constant defines it, with what it needs, before its first declaration.
- [Known gaps](known-gaps.md): A response file is opaque to .metal routing and Apple's metal output is not reproducible.
- [Translating a Makefile](makefile-translation.md): smith build --dump-config reads GNU make's common subset and writes a build.c in the low-level API, reporting what it cannot read.
- [Packages, smith_package and smith fetch](packages.md): A package is a URL and a hash fetched by smith's own HTTPS client into the cache, with no registry and no lock file.
- [Protocol code generated from the official schemas](protocol-generation.md): The DAP and MCP protocol code is a build output of scripts/protocol.c, written from the schemas vendored in schemas/, never committed.
- [Sanitized processes start as fast as Apple's](sanitizer-startup.md): A two-line compiler-rt patch gives AddressSanitizer's shadow the fast mmap flag on arm64, so a sanitized process starts in 9 ms.
- [smith is built by its previous release](smith-bootstrap.md): A clone with no smith downloads the release of its newest ancestor commit and builds with it; no committed copy of the libraries, no stage0 built by Homebrew's clang.
- [Generic functions are lowered, not templates](smith-generics.md): constexpr type parameters and auto parameters make one weak hidden instance per list of types, chosen and written by the lowering before the parser.
- [How the lowering works](smith-lowering-internals.md): The lowering is a state machine on the token hook, written against runtime/lang.h, with a table of the names it made and a small interface to clang.
- [Names as written in the editor, lint and diagnostics](smith-names-as-written.md): smith lsp and smith lint lower .smith files too, and report names, notes and fix-its as the source wrote them, never guessed from underscores.
- [smith new writes the program and what agents need](smith-new.md): smith new writes a program, its tests and the agent documents, with no configuration, and builds and commits once.
- [How smith build builds .smith sources](smith-sources-in-build.md): A header step per module comes first and every .smith compile waits for all of them.
- [The test beds and the bugs checkers found](test-beds.md): checkers, learner and game are the projects smith is tried on, and checkers' tests found four bugs fixed at their source.
