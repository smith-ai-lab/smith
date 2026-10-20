# Roadmap

In order. Each step starts only with the owner. AGENTS.md holds the rules; this file holds where smith is going.

1. **libclang dry run.** When asked (an environment variable), smith parses each translation unit it compiles with the
   arguments it was given, plus `-resource-dir` (as `learner/tools/lint` does), and changes no output. It prints
   function and assert counts for checkers, learner and game. smith sees every compile's exact arguments, so this needs
   no `compile_commands.json`.
2. **Contracts** (`requires`, `ensures`, `invariant`) checked with Z3, first on checkers, then `game/src/base/arena.c`.
   The first goal: smith reports `get_user_move`'s out-of-bounds read by itself. What the solver cannot prove becomes an
   always-on runtime check, and later a Lean theorem to prove by hand. Lean needs a model of the C we write; no malloc,
   no recursion, bounded loops and arenas keep that model small. `BASE_ASSERT` needs no change: after preprocessing it
   is a branch into a `[[noreturn]]` function, which is an assertion.
3. **Subcommands that forward**, recognised only as the exact first argument so no clang command is mistaken for one.
   With arguments they forward them unchanged; with none they act on the whole project. From the command line they
   behave exactly as the tools do alone: a project's `.clang-format` and `.clang-tidy` apply as usual, and without
   them clang-format's and clang-tidy's own defaults apply. smith adds no style of its own; its house style is used only
   when a project names it (step 4).
   - `smith fmt`: the pinned clang-format.
   - `smith lint`: clang-tidy, plus smith's own rules through libclang. learner's and game's shared Tiger Style rules
     move here from their `tools/lint`.
   - `smith dbg`: the platform's lldb.
4. **`build.c` and `smith build`, `smith run`, `smith test`.** Only C, no `build.cpp`.
   - `build.c` is an explicit, cross-platform description of a build, written to replace a Makefile one for one. Every
     flag, file and step is written out, it has no defaults, and adopting it changes nothing.
   - `build.c` emits ninja first. The executor can change later without anyone rewriting their `build.c`.
   - v1 is done: [docs/build-c.md](docs/build-c.md), and checkers and smith itself build with it (smith's `build.c`
     replaced its Makefile). Next, learner's Makefile in a scratch copy, compared byte for byte with make; then v1.5
     (`smith_defer`, `smith_lines`) for game.
   - `compile_commands.json` comes from `-MJ` fragments merged by a step, as in checkers, or from `smith build -t
     compdb`. smith adds nothing for it.
   - Conveniences sit on top, each a call a project makes by name. The first is the smith house style. A target that
     opts in gets:
     - `-std=gnu23`, all warnings with `-Werror`, and debug information;
     - sanitizers in debug and test builds;
     - `-ftrivial-auto-var-init=pattern` and `-ffp-contract=off`;
     - a named CPU rather than `-march=native`;
     - smith's `.clang-format`, `.clang-tidy` and Tiger Style lint rules for `smith fmt` and `smith lint`.
   - `smith test` bakes in the convention game already uses:
     - tests are non-`static` `void test_<name>(void)` functions in `test_*.c` files, found through libclang;
     - `test_death_<name>` must die;
     - `void test_prop_<name>(u64 seed)` runs over many seeds and prints the failing one.
     
     smith generates the runner: one process per test, in parallel. The tests compile with the flags `build.c` gives
     them; nothing is added by default. A project can choose the files, the links and the flags, or keep its own
     runner and take only the generated list of tests.
5. **`.smith` files**: C plus syntax that replaces macros, lowered to plain C with `#line` so clang, lldb and the checker
   keep working. The syntax stays valid C where it can, so existing tools keep working; compiling through clang's
   and LLVM's libraries instead of emitting C is for later, if emitting C proves too slow. First-class types run at
   compile time (`type f(type t)`, like Zig's `comptime`) and replace `ARENA_PUSH` and `ARENA_PUSH_ARRAY`.
   `BASE_COUNT` needs nothing new (`_Countof`). `BASE_ASSERT` needs the caller's file, line and expression text. A
   `test "name" { ... }` block can lower to the same test functions. Shapes as dependent types (matrix sizes,
   lengths) later.
   - smith stays plain C until `.smith` is stable. Then smith can be written in smith: built by the previous release,
     pinned by version and checksum, with each release's lowered C as a bootstrap that needs only `cc`.
6. **One download**:
   - `smith lsp` wraps the platform's clangd (Apple's on macOS) and adds `.smith`.
   - `smith prove` runs the contracts: Z3, then Lean.
   - `smith check` runs the whole gate: `fmt --check`, `lint`, `prove` and `test`.
   - A pinned clang-format and clang-tidy ship beside smith. On macOS the compiler, SDK, frameworks and `metal` still
     come from Xcode or the Command Line Tools.

Later ideas, not planned: arena regions and `inout` modes; cost clauses (flops, bytes) and bounded recursion with a
decreasing measure; a kernel subset of C emitted as `.metal`; a tree-sitter grammar for `.smith`. The owner wants no
macros in the long run, and one language for specs and code, with proof helpers erased at compile time. Real-time work
can build on LLVM's `[[clang::nonallocating]]` and `[[clang::nonblocking]]` (checked at compile time) and
RealtimeSanitizer (`-fsanitize=realtime`, at run time).
