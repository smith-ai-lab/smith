# smith

A drop-in compiler driver for C, Objective-C, C++ and Metal, written in C. Milestone 0, a routing passthrough, works on
macOS: smith builds itself, checkers, learner and game, and `smith build` builds smith, checkers and game from their
`build.c`. Next are contracts checked at compile time, then `.smith` files whose syntax replaces macros; the order is in
[ROADMAP.md](ROADMAP.md).

## Decisions (change one only with the owner)

- **The best compiler on each platform.** On macOS that is Apple's: `/usr/bin/clang` (it runs through xcrun, which
  finds the SDK) for C, C++ and Objective-C, and `metal` (`xcrun -sdk macosx metal`, or `$SMITH_METAL`) for `.metal`.
  Upstream's AddressSanitizer costs about 40 ms per process there, in LLVM 22 and 23 alike, against 3 to 9 ms for
  Apple's; it made game's sanitized tests 13 s instead of 2.4 s. Elsewhere, upstream clang. `.metal` and other inputs
  in one command are refused.
- **clang-format and clang-tidy come from one pinned upstream LLVM** on every platform, so formatting and lint are the
  same everywhere: Homebrew `llvm` 23.1.3, held with `brew pin`. Apple ships no clang-tidy and an older clang-format.
  On macOS, clangd and lldb are Apple's, matching the compiler.
- **smith never changes a default, so adopting it is trivial and each step is optional.**
  1. **Use the smith command instead of clang.** For C, C++ and Objective-C it is a pure passthrough: smith never adds,
     removes or reorders flags, so the build cannot change.
  2. **Replace the Makefile with a `build.c`.** It only describes the build, explicitly and across platforms, so it
     changes nothing either.
  3. **Run `smith new`.** It writes an explicit, opinionated starting point (C2y with `-std=gnu2y`, for `_Countof`;
     strict warnings; three builds: debug with sanitizers, release with assertions on, fast without; `.clang-format`,
     `.clang-tidy`, `.claude/settings.json` enabling the smith plugin, git on `main`, a first build), all in files the
     project owns. smith's skills are not copied: they ship in the smith plugin. There is no house-style mechanism,
     only those files; a project that never runs `smith new` gets none of it.
- **`smith++` is smith under another name**, as clang++ is to clang. Compiling is identical; linking C++ needs it.
- **smith `execv`s the compiler** with the compiler's full path as `argv[0]` (gcc finds its files from it), so exit
  status, signals and pipes are the compiler's.
- **C23 (`-std=gnu23`), libc only, no malloc.**
- **Contracts are C23 attributes that smith reads through libclang**, loaded with `dlopen` only when a check runs. Not a
  clang plugin: a plugin's `[[smith::requires(...)]]` receives no arguments, and its `__attribute__` form cannot see the
  parameters (tested on clang 22). An unknown attribute fails `-Werror` builds, so the spelling in `.c` files is open.
- **`smith build` runs a project's `build.c`, which writes a ninja manifest, then runs ninja.** It is recognised only
  as the exact first argument. The header and runtime are embedded in smith with `#embed`, so smith still builds with
  one `cc` command. Design and API: [docs/build-c.md](docs/build-c.md).
- **Contracts do not depend on `-fbounds-safety`**, which only Apple's clang has. UBSan and the arenas' ASan poisoning
  check bounds at run time; contracts will prove them.
- **smith's libraries are a modern standard library for C, holding only what is proven.**
  - **Joining:** a library joins when two projects need the same thing (game's trigonometry and random's logarithm make
    `<smith/math.h>`), never on speculation. So there are no vectors, tensors, platform layer or async IO until a second
    project needs one.
  - **What one looks like:**
    - It does one thing and is named for it, so there is no "base" or "utils".
    - It uses standard types: `<stdint.h>`, `size_t`, `float` and `double`, with no aliases.
    - It uses the standard `assert`, and macros only where a function cannot do the job, such as capturing a check's
      expression text.
    - Its names are typed as `<math.h>` names them, with no `_Generic`.
  - **How a project uses one:** it compiles the library from source with its own flags (`smith_library`).
  - **Platform code:** code that touches the OS stays in each project's platform layer, written against that platform's
    best API.

## Commands

- smith builds itself with its own `build.c`, so the installed smith must have `smith build`. `smith build` builds
  `build/<OS>-<arch>/smith` and the `smith++` link; `smith build test` runs the routing tests against stub compilers,
  then the `smith build` tests with real ninja and clang; `smith build compare -DREAL=/usr/bin/clang` runs the same
  invocations through clang and smith.
- `smith build check` checks formatting and clang-tidy (the same configs as game, build.c included), then runs the
  `test` and `compare` targets; it must pass before a change is done. `smith build format` rewrites the sources.
- `smith build install` runs the tests, then copies smith and the `smith++` link to `$PREFIX/bin` (`-DPREFIX=`,
  default `~/.local/bin`), and a build uses it when CC or CXX names smith. A broken install breaks every build,
  smith's included. The way back needs only a C23 compiler, and is also the first build of a fresh clone:
  `mkdir -p build && cc -std=gnu23 smith.c -o build/stage0 && build/stage0 build install -DCC=cc`. `-DCC=` picks the
  compiler that builds smith; `smith build clean` removes `build/`.
- `smith new [<name>]` writes a ready project into `<name>` (or the current folder), never overwriting: `build.c`,
  `main.c` (a few lines of modern C), `.gitignore`, `.clang-format`, `.clang-tidy` and `.claude/settings.json` (which enables the smith plugin
  and declares its marketplace), then builds once and commits on `main`. No skills are copied. `tests/new.sh` tests it.
- `SMITH_CLANG`, `SMITH_CLANGXX` and `SMITH_METAL` pick the compilers, `SMITH_VERBOSE=1` prints the command, and
  `smith --smith-which <args>` prints the choice without running it. `SMITH_NINJA` picks the ninja `smith build`
  runs.

## How to work

- Load the [smith-development skill](.agents/skills/smith-development/SKILL.md) for any change to smith itself.
- Load the [smith skill](.agents/skills/smith/SKILL.md) when using smith. It ships in the smith plugin, from `.agents/skills`; no project carries a copy.
- Load the [smith-build skill](.agents/skills/smith-build/SKILL.md) to run, write or edit a build.c, smith's own included. It ships in the smith plugin, from `.agents/skills`; no project carries a copy.
- Run the real thing and report numbers. Say plainly what was not tested.
- `~/Documents/checkers`, `~/Documents/virtual-cell/learner` and `~/Documents/game` are the test beds: read them, build
  them in scratch copies, and change their sources only with the owner. When smith changes one, leave its agents a
  note on what changed and why.
- Small Conventional Commits.
- Hand work to subagents whenever possible, on Sonnet 5.5 at medium reasoning effort.

## Bugs the checker must find (`~/Documents/checkers/checkers.c`)

- `get_user_move` tests `from_char <= 'H'` twice and never `to_char`, so column I reads `cells[row][8]` (line 112).
- At end of input `get_user_move` loops forever: `scanf` returns `EOF` and nothing ends the loop.
- `get_valid_diagonals` and `get_valid_jumps` miss legal moves (`else if`; `col > 1`, `row > 1`, `col > 2` off by one):
  419,348 over 200,000 random boards, and no illegal move generated.
- `apply_follow_up_jumps` recurses, bounded by 12 captures.

## Known gaps

- A response file (`@file`) is opaque, so a `.metal` input inside one goes to clang.
- Apple's `metal` output is not reproducible (two identical builds of learner's shaders differ), so compare shaders by
  the command smith runs, not by bytes.
- Why upstream's ASan is slow on macOS is not known: the time is kernel time, in both `fork` and `posix_spawn`, which
  only root can trace.
