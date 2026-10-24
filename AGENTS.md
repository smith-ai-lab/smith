# smith

A C toolchain and a modern standard library for C, written in C. Each fact has one home: rules and decisions here, one line each; what smith is in [knowledge/](knowledge/index.md) (read only the concepts your task needs); how to do a task in `.agents/skills/` (smith-dev for changing smith, the plugin's skills for using it); planned work in [GitHub issues](https://github.com/adam-r-kowalski/smith/issues) and nowhere else.

## Decisions (change one only with the owner)

**Compiler and tools**
- One compiler, upstream LLVM 23.1.3, built by `smith build llvm` and linked into smith; no LLVM install at run time; Homebrew's pinned `llvm` is only what the stage0 runs; `.metal` goes to Apple's `metal`. [one-compiler](knowledge/one-compiler.md), [llvm-build](knowledge/llvm-build.md)
- smith compiles in its own process (`clang_main`); `SMITH_CLANG` runs an external clang; a change of compiler rebuilds every object. [one-compiler](knowledge/one-compiler.md)
- The files the driver looks up are embedded and written once to a runtime folder that smith never removes; the programs are clones (never hard links) in a per-binary folder. [embedded-files-and-programs-folders](knowledge/embedded-files-and-programs-folders.md)
- The linker is lld in smith's process; the system's other tools (`dsymutil`, `libtool`, `xcrun`) stay the system's. [linker](knowledge/linker.md)
- The SDK is chosen through the environment, never a flag; the caller's own `SDKROOT`, `-isysroot` and `--config` win. [sdk-rule](knowledge/sdk-rule.md)
- Sanitized processes start as fast as Apple's, through a two-line compiler-rt patch. [sanitizer-startup](knowledge/sanitizer-startup.md)
- `smith fmt`, `lint` and `lsp` are clang-format, clang-tidy and clangd from that LLVM, with smith's defaults only where a project has no file, and no `--query-driver` for the editor. [format-lint-and-editor-tools](knowledge/format-lint-and-editor-tools.md)
- `smith dbg` and `smith dap` are lldb and lldb-dap from the same LLVM, with the system's debugserver, no Python and no curses. [debugger](knowledge/debugger.md)
- Contracts are calls read by `smith prove` (Alive2, Bitwuzla, Z3), not a clang plugin and not `-fbounds-safety`; the boundary is `smith_trusted()` and there is no list. [contracts](knowledge/contracts.md), [prover](knowledge/prover.md)
- A proof is fast or it is written wrong, and fast is CPU time (about 10 s on 8 idle cores). [proof-budget](knowledge/proof-budget.md)
- Enzyme is linked into smith's lld; an autodiff program links with full LTO, and debug has ASan and no UBSan. [autodiff](knowledge/autodiff.md)

**Adoption and the verbs**
- Adopting smith is incremental from both ends and every step is optional; `smith cc` never changes a default. [adoption-path](knowledge/adoption-path.md)
- `smith new` writes the program and what agents need, and no configuration: "we have to assume the primary author is going to be Claude or some other agent." [smith-new](knowledge/smith-new.md)
- smith's top level is only verbs; the bare `smith foo.c` is an error; `smith-cc` and `smith-c++` are links for what needs one program path; `smith++` is gone. [verbs](knowledge/verbs.md)
- The verbs have fixed meanings and a project's own tasks run with `smith build <task>`; `smith_task` and `smith_alias` refuse a verb's name. [verbs](knowledge/verbs.md)
- `smith docs` checks AGENTS.md, `knowledge/` (OKF) and `.agents/skills/`, and `smith new` starts every project with them. [smith-docs skill](.agents/skills/smith-docs/SKILL.md)
- `smith build` runs build.c, then its own executor, with no ninja (`SMITH_NINJA` is the way out). [build-file](knowledge/build-file.md), [executor](knowledge/executor.md)
- A Makefile is translated by smith's own reader of GNU make's common subset; no vendored make (GPLv3, no license forced on smith). [makefile-translation](knowledge/makefile-translation.md)
- Clang modules are implicit, one per header, opt-in; there are two caches with one 31-day rule; packages are a URL and a hash, and smith reads its archives itself. [clang-modules](knowledge/clang-modules.md), [caches](knowledge/caches.md), [packages](knowledge/packages.md), [archives](knowledge/archives.md)

**Language and checks**
- smith's own code is C23, libc only, no malloc; the only C++ is the thin shims in `shim/`. [smith-own-code](knowledge/smith-own-code.md)
- A failure's report is read from its call stack, so checks and contracts are plain functions; a test's signature says what it may touch. [failure-reports-and-tests](knowledge/failure-reports-and-tests.md)
- `.smith` files are C with modules, lowered to C as clang reads them through a token hook; modules use `::`, never dots. [smith-language](knowledge/smith-language.md), [smith-lowering-internals](knowledge/smith-lowering-internals.md)
- A `.smith` file has no preprocessor; the target's facts are `target::`; `smith-preprocessor` enforces it. [smith-target-facts](knowledge/smith-target-facts.md), [smith-preprocessor-check](knowledge/smith-preprocessor-check.md)
- The C rule: valid C in a `.smith` file without a `module` line means exactly what it means in C, in the latest C by default (an older C is an explicit `-std=`); a new feature uses syntax that is invalid C, and a `module` line may reject valid C but never silently changes it; `tests/smith-lang-c.sh` checks it. [smith-language](knowledge/smith-language.md)
- Smith is C plus compile time: every addition is a C construct evaluated at compile time (`if constexpr`, `for constexpr` as C's own `for`, `countof`, constant indexing) or fills a gap or removes a hazard of C (modules, sized types, `auto x = {...}` lists, which C23 leaves invalid); never user-defined dispatch (no overloading, traits or operator overloading), and every addition lowers to plain C. [smith-language](knowledge/smith-language.md)
- Generic functions are lowered, not templates; every compile-time parameter is `constexpr`. [smith-generics](knowledge/smith-generics.md)
- Compile-time power is ordinary functions, not compiler builtins: what the compiler computes about types and values, a user's function can compute too (owner: "users have as much power as the compiler"); a builtin is added only where no function can express it. [smith-generics](knowledge/smith-generics.md)
- `smith lsp` and `smith lint` handle `.smith` files as written, with no LLVM change beyond the hooks. [smith-names-as-written](knowledge/smith-names-as-written.md)
- A module's generated header includes exactly what its exports need. [smith-module-headers](knowledge/smith-module-headers.md)
- The idiom checks and `smith-effects` are clang-tidy checks, on in smith's default `.clang-tidy`; a program touches the world only through capabilities passed down from `main`. [idiom-checks](knowledge/idiom-checks.md), [effects-check](knowledge/effects-check.md)
- smith's libraries are a modern standard library for C holding only what is proven: a library joins when two projects need it, does one thing, defines no macros. [libraries](knowledge/libraries.md)
- The protocol code of `smith mcp` is generated, never written by hand; `smith mcp` is the debugger as MCP tools. [protocol-generation](knowledge/protocol-generation.md), [mcp-server](knowledge/mcp-server.md)
- Neovim support is this repository and enables nothing the user did not ask for. [neovim](knowledge/neovim.md)

## How to work

- Load [smith-dev](.agents/skills/smith-dev/SKILL.md) for any change to smith itself; it names the small skills (build, LLVM, tests, verbs, language, libraries, bootstrap, C, docs).
- Load [smith](.agents/skills/smith/SKILL.md), the map, when using smith; it names the small skills for the compiler, `build.c`, `run` and `test`, `check`, `fmt`, `lint` and `lsp`. They ship in the plugin from `.agents/skills`; no project carries a copy.
- Load [smith-build](.agents/skills/smith-build/SKILL.md) to run, write or edit a build.c, smith's own included.
- `~/Documents/checkers`, `~/Documents/virtual-cell/learner` and `~/Documents/game` are the test beds: read them and run the installed smith on them in place; build, test or prove them with a smith under test only in a scratch copy, so lanes never share a `.smith/`. They follow smith: when smith changes its idiom or breaks their code, change them to match (the owner's own projects for trying smith: commit and push to their main freely); the commit says what changed and why, naming the smith issue, and their AGENTS.md carries no change log (the plugin's skills teach the current idiom). [test-beds](knowledge/test-beds.md)
- The plugin's skills describe everything smith does today and nothing it doesn't do yet: every command, build.c call and library has a skill another project's agent can learn it from; plans go in GitHub issues, never in a skill; a change that adds or changes something updates its skill and its concept in the same commit. Skills stay under about 60 lines.
- `smith new`'s program (`templates/new`) is the example of idiomatic Smith: a change to the idiom (a library becoming a module, a new rule) updates it in the same commit, and `tests/new_test.smith` checks that the new project passes `smith check`.
- Small Conventional Commits. Push `main` after every merge: the plugin's skills come from GitHub, so other agents learn a feature only once it is pushed. The commit that finishes an issue says `Closes #N`, a partial step `Part of #N`.
- Finish open issues before starting new work; a new idea becomes an issue, not a branch. The order is the pinned roadmap, #73: take the first unchecked item of a track nothing running blocks, and give a new issue its place there.
- Hand work to subagents whenever possible, on Sonnet 5.5 at medium reasoning effort.
- Fix what clang-tidy reports; do not weaken `.clang-tidy`. Subagents never run `smith build install` (the orchestrating agent installs after a merge); never `pkill` or `killall`.
- Known gaps: [known-gaps](knowledge/known-gaps.md).
