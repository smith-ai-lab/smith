# Roadmap

In order. Each step starts only with the owner. AGENTS.md holds the rules; this file holds where smith is going.

1. **`compile_commands.json` as a side effect** of each compile (one fragment per file, merged), replacing the `-MJ` and
   `sed` lines in checkers.
2. **libclang dry run.** Parse every translation unit of checkers, learner and game with the same arguments plus
   `-resource-dir` and `-isysroot` (as `learner/tools/lint` does), change no output, print function and assert counts.
3. **Contracts** (`requires`, `ensures`, `invariant`) checked with Z3, first on checkers, then `game/src/base/arena.c`.
   What the solver cannot prove becomes an always-on runtime check, and later a Lean theorem to prove by hand. Lean needs
   a model of the C we write; no malloc, no recursion, bounded loops and arenas keep that model small. `BASE_ASSERT`
   needs no change: after preprocessing it is a branch into a `[[noreturn]]` function, which is an assertion.
4. **`.smith` files**: C plus syntax that replaces macros, lowered to plain C with `#line` so clang, lldb and the checker
   keep working. First-class types run at compile time (`type f(type t)`, like Zig's `comptime`) and replace
   `ARENA_PUSH` and `ARENA_PUSH_ARRAY`. `BASE_COUNT` needs nothing new (`_Countof`). `BASE_ASSERT` needs the caller's
   file, line and expression text. Shapes as dependent types (matrix sizes, lengths) later.
5. **One download**: smith as the formatter, language server and debugger. It wraps the platform's clangd and lldb
   (Apple's on macOS) and a pinned clang-format and clang-tidy shipped beside it. Subcommands must never be mistaken
   for clang arguments (hence `--smith-which`). On macOS the compiler, SDK, frameworks and `metal` come from Xcode or
   the Command Line Tools.
6. **`build.c`** that emits ninja.

Later ideas, not planned: arena regions and `inout` modes; cost clauses (flops, bytes) and bounded recursion with a
decreasing measure; a kernel subset of C emitted as `.metal`; a tree-sitter grammar for `.smith`. The owner wants no
macros in the long run, and one language for specs and code, with proof helpers erased at compile time. Real-time work
can build on LLVM's `[[clang::nonallocating]]` and `[[clang::nonblocking]]` (checked at compile time) and
RealtimeSanitizer (`-fsanitize=realtime`, at run time).
