# smith

A C toolchain and a modern standard library for C, written in C. Planned work is in GitHub issues
(https://github.com/adam-r-kowalski/smith/issues), and nowhere else.

## Decisions (change one only with the owner)

- **One compiler, upstream LLVM: the one smith ships.** It is smith's own static build of LLVM 23.1.3 (`smith build
  llvm`), linked into smith, which compiles C, C++ and Objective-C in its own process, with no LLVM install needed at
  run time. Homebrew's `llvm` 23.1.3, held with `brew pin`, is what a smith without clang inside (the stage0) runs.
  `.metal` goes to Apple's `metal` (`xcrun -sdk macosx metal`, or `$SMITH_METAL`), and `.metal` and other inputs in one
  command are refused.
  - **The files the driver looks up beside itself are embedded** (`scripts/pack.c`, `#embed`, LZFSE; about 20 MB in
    smith): the builtin headers, the sanitizers' ignore lists, the macOS compiler-rt runtimes (not iOS's), and
    `libLTO.dylib`, which the system `ld` loads for `-flto` (lld has LLVM inside and needs none). On first use (about 1 s, then a `stat`) smith writes them
    to `$XDG_DATA_HOME/smith/clang/llvm-<version>-<key>` (else `~/.local/share/...`); the key is the pack's contents
    and the identity of the compiler and linker. The programs the driver runs (`bin/clang`, `bin/clang++`, `bin/ld`,
    hard links to smith itself; it runs `clang` again with `-cc1` for any compile with more than one step) are not in
    it: they are in `$XDG_DATA_HOME/smith/programs/<LC_UUID of the running smith>/`, whose `lib` links to that
    folder's, and the shim passes `-resource-dir` for it. So the installed smith, a dev build and another agent's
    never share or remake each other's links, and a smith without lld has no `ld`. A programs folder idle for 31 days
    is removed when a new one is made (using one touches it once a day); only the runtime folder lives forever.
  - **That folder is outside the caches smith prunes and cleans, and smith never removes it**, because a sanitized
    binary records an absolute `LC_RPATH` to its `lib/clang/23/lib/darwin`: `smith cache clean`, the 31-day pruning and
    an emptied `~/.cache` leave every built binary running. Old folders stay for the binaries built against them (about
    70 MB each); delete one by hand to reclaim it, and its binaries then fail at load, naming the missing library.
  - **The linker is lld, in smith's process.** The driver looks for `ld` in its own program's folder before `PATH`, so
    that folder's `bin/ld` is a third hard link to smith; run as `ld`, smith calls `lld::macho::link` (`shim/lld.cpp`)
    on the arguments the driver built, so nothing is added to the user's command and no system `ld` runs. lld signs
    the output ad hoc on Apple Silicon, as `ld` does. The driver starts it as a process of its own (it has no hook to
    link in its own process, as it has for `-cc1`): one more start of smith, which costs less than ld's own start (a hello-world link takes 33 ms against ld's 48). `SMITH_LINKER=/usr/bin/ld`
    makes smith run that linker instead (a way out, and for comparison), and then the driver is left as Apple's `ld`
    wants it. With lld the driver is told the linker's version (`-mlinker-version=1115.7.3`, which the user's own
    flag overrides), so that it does not send messages to classes through `objc_msgSendClass$` stubs, which only
    Apple's current `ld` writes; lld writes `objc_msgSend$` stubs per selector.
  - **The system's other tools stay the system's**, found on `PATH`: `dsymutil` (Apple's; its output is the same size
    as LLVM's in `compare`), `libtool`, `xcrun`.
  - **The SDK is chosen through the environment, never a flag.** Homebrew's clang reads a default config
    (`/opt/homebrew/etc/clang/*.cfg`) that forces `-isysroot` to the Command Line Tools SDK, and that wins over
    `SDKROOT`, though `xcrun` and `metal` use Xcode's. So on macOS smith sets, for each the caller has not set,
    `CLANG_NO_DEFAULT_CONFIG=1` and `SDKROOT=$(xcrun --show-sdk-path)`. The caller's own `SDKROOT`, `-isysroot` and
    `--config` win.
  - **The cost is in sanitized processes.** Upstream's AddressSanitizer starts a process in about 80 ms against 18 ms
    for Apple's (measured here, 40 runs of an empty sanitized program). game's sanitized tests, run three times each
    after a clean build: 5.0, 5.5 and 5.8 s under Homebrew's clang, 2.9, 3.0 and 2.8 s under Apple's (a second round on
    a busier machine: 6.9 to 7.2 s against 4.5 to 4.8 s).
- **`smith fmt`, `smith lint` and `smith lsp` are clang-format, clang-tidy and clangd, from the LLVM smith compiles
  with** (linked into smith; the stage0 runs Homebrew's, or `$SMITH_CLANG_FORMAT`, `$SMITH_CLANG_TIDY`,
  `$SMITH_CLANGD`), so formatting, lint and the editor are the same everywhere, and each takes the tool's own arguments.
  Point an editor at `smith lsp` with no `--query-driver`: with it clangd puts the SDK's include paths ahead of
  clang's own headers, and Apple's SDK module map refuses that order, so it fails on modules.
  - **`.smith` is C to clang-format** (issue #25; `patches/clang-format-smith.patch`, about 100 lines): C's keywords
    only, `::` one token, and `module ...;` and `import ...;` lines at file scope kept whole (`import <sys/wait.h>;`).
    Inside a function `import` and `module` are names, as in C. clangd formats `.smith` the same way (same library).
- **`smith dbg` and `smith dap` are lldb and lldb-dap, from the same LLVM** (linked in; the stage0 runs `$SMITH_LLDB`
  and `$SMITH_LLDB_DAP`), with the system's debugserver (Apple signs it), no Python and no curses
  ([docs/debugging.md](docs/debugging.md); the skill is smith-debug). They add about 17 MB to smith and, because the
  frameworks lldb needs load with it, about 9 ms to the start of every smith process.
- **smith never changes a default, so adopting it is trivial and each step is optional.**
  1. **Use `smith cc` instead of clang** (`CC=smith-cc` in a Makefile, where one program path is needed). For C,
     C++ and Objective-C it is a pure passthrough: smith never adds, removes or reorders flags, so the build cannot
     change.
  2. **Replace the Makefile with a `build.c`.** It only describes the build, explicitly and across platforms, so it
     changes nothing either.
  3. **Run `smith new`.** It writes an explicit, opinionated starting point (C2y with `-std=gnu2y`, for `_Countof`;
     strict warnings; three builds: debug with sanitizers, release with assertions on, fast without; `.clang-format`,
     `.clang-tidy`, `.claude/settings.json` enabling the smith plugin, `AGENTS.md`, `knowledge/` and a docs skill, git on `main`, a first build), all in files the
     project owns. smith's skills are not copied: they ship in the smith plugin. There is no house-style mechanism,
     only those files; a project that never runs `smith new` gets none of it.
- **smith's top level is only verbs**, as zig's is: `smith cc` and `smith c++` compile (clang's arguments, unchanged);
  `build`, `new`, `fetch`, `cache`, `docs`, `version`, `fmt`, `lint`, `lsp`, `run`, `test` and `check` do the rest. The bare form `smith
  foo.c -o foo` is an error that says to run `smith cc foo.c -o foo`, so nothing is ambiguous. For what needs one
  program path (`CC=` in a Makefile, CMake's `CMAKE_C_COMPILER`, the first word of a `compile_commands.json` command,
  which clangd reads as a compiler), `smith build install` also installs `smith-cc` and `smith-c++`, links to smith that
  act as `smith cc` and `smith c++` by their name, as `smith++` did. `smith++` is gone. The program a `build.c` names as
  a compiler is `smith-cc` or `smith-c++`; `"smith"` is refused with that hint.
- **`smith docs` checks a project's AGENTS.md, `knowledge/` (OKF) and `.agents/skills/`, and `smith new` starts every
  project with them** (`runtime/docs.c`; skill smith-docs); concept types are options of the check, set in build.c.
- **A project verb runs the task or alias of its name in the project's `build.c`**: `smith run`, `smith test`,
  `smith check`, and `smith fmt` and `smith lint` with no arguments (with arguments they are clang-format and
  clang-tidy). There is no second list of what a project is: the build file defines the targets, as it defines
  everything, and `smith_tests` already names `test`. `smith run` and `smith test` give their arguments (not their `-D`
  options) to the task through `smith_args(b)`: the program's for `run`, the test runner's own options for `test`
  (`--test`, `--seed`, `--jobs`, `--full`, ...). `smith prove` is the prover with arguments and the project's `prove` task with none;
  `smith dbg` comes with issue #7 and says so until then (a row of the verb table in `smith.c`). The verbs are not targets of `smith build`: `smith build test`
  is refused with a pointer to `smith test`, so each thing has one way.
- **smith compiles in its own process**, by running clang's own `clang_main` on the arguments (`shim/clang.cpp`), so
  every flag, diagnostic, exit status, signal and crash report is clang's, and so are pipes. `SMITH_CLANG` runs an
  external clang instead, `execv`ed with its full path as `argv[0]` (gcc finds its files from it), as `.metal` runs
  Apple's `metal`.
- **A change of compiler rebuilds every object it compiled.** An object depends on a file in `.smith/compiler/` that
  names its compiler, rewritten only when that changes. The compiler is its executable (path, size and mtime), but for
  smith it is the clang it runs, not smith's own file: the LLVM version and a hash of the archives linked in
  (`lib/smith-clang.id` of the install, compiled into smith, printed by `--smith-which`), or for `SMITH_CLANG` that
  clang's file. So a smith rebuilt with the same LLVM rebuilds nothing, and a rebuilt LLVM or another clang rebuilds all.
- **smith's own code is C23 (`-std=gnu23`), libc only, no malloc**: it allocates nothing itself. The LLVM linked into
  it is C++ and does, and the files in `shim/` (`clang.cpp`, `lld.cpp`, `symbolize.cpp`), thin C interfaces to it, are
  the only C++ in smith.
- **A failure's report is read from its call stack, so checks and contracts are plain functions.** The program walks its
  stack by frame pointers and writes the addresses with each image's UUID and slide (`<smith/trace.h>`); it holds no
  symbolizer. smith turns them into the failing line, its source and the call chain with LLVM's symbolizer, reading
  the debug map of the binary (its object files), not a dSYM: a failure costs about 8 ms beyond smith's start, `dsymutil`
  40 to 150 ms at every link. `smith build test` pipes the runner through `smith symbolize`; smith run as
  `llvm-symbolizer` serves the sanitizers. `SMITH_CHECK` is kept, deprecated; `SMITH_CHECK_ABORTS` stays a macro.
- **`.smith` files are C with modules, lowered to C as clang reads them** ([docs/smith-lang.md](docs/smith-lang.md), issue
  #11). The one change to clang is a token hook on its `Preprocessor` (`patches/clang-token-hook.patch`, about 100
  lines, applied by `scripts/llvm.sh`), which offers a `TokenHook` the identifiers it watches, after macro expansion and
  before the parser; plain C in a `.smith` file costs the compiler under 1%. The language is smith's own C
  (`runtime/lang*.c`, which knows nothing of clang), and the clang side is `shim/hook.cpp`: the hook, and the parser's
  name lookup as the lowering's questions. Modules use `::` (`module a::b;`, `x::name`, `c::printf`), never dots. The
  `.smith` extension turns it on (`smith cc x.smith` is `-x c x.smith`); plain C in a `.smith` file means what it means
  in a `.c` file (`tests/smith-lang-c.sh`). A smith whose clang lacks the hook, a stage0 included, refuses a `.smith`
  input; it can still write a module's header (`smith cc --smith-header`). The compiler's helpers are under `smith cc`
  (`--smith-header`, `--smith-lower`, `--smith-which`), since smith's top level is only verbs. `smith build` builds the
  modules itself: a header step per module, and every `.smith` compile waits for all of them (docs/build-c.md).
- **Contracts are C23 attributes that smith reads through libclang**, loaded with `dlopen` only when a check runs. Not a
  clang plugin: a plugin's `[[smith::requires(...)]]` receives no arguments, and its `__attribute__` form cannot see the
  parameters (tested on clang 22). An unknown attribute fails `-Werror` builds, so the spelling in `.c` files is open.
- **`smith build` runs a project's `build.c`, which writes a ninja manifest, then runs that manifest itself, with no
  ninja.** The executor (`runtime/executor*.c`, on `<smith/io.h>`) reads the subset of ninja's language that smith
  writes and keeps its own state in `.smith/`; `SMITH_NINJA=<ninja>` runs the manifest with ninja instead. It is
  recognised only as the exact first argument. The header and runtime are embedded in smith with `#embed`, so the stage0 (smith without
  clang inside: no C++, no LLVM, it runs an external clang) still builds with one `cc` command. Design and API: [docs/build-c.md](docs/build-c.md).
- **Clang modules are implicit, one module per header, and opt-in.** `smith_modules` in build.c writes the maps under `.smith/modules/`; clang keeps the cache in `$XDG_CACHE_HOME/smith/modules` (else `~/.cache/smith/modules`), one for every project and worktree, so the SDK's modules are built once per machine. One module per header (not an `umbrella` directory), because the depfile of an umbrella module lists every header in the folder and an edit would rebuild every source. compile_commands.json keeps the module flags (`smith lsp` builds them, with no `--query-driver`). `smith new` turns them on; [docs/build-c.md](docs/build-c.md) has the design.
- **Contracts do not depend on `-fbounds-safety`**, which only Apple's clang has. UBSan and the arenas' ASan poisoning
  check bounds at run time; contracts will prove them.
- **`smith prove` checks LLVM IR with Alive2, Bitwuzla and Z3, linked into every full smith.** Contracts are
  the calls `smith_requires`, `smith_ensures` and `smith_invariant` of `<smith/contract.h>`. There is one LLVM configuration
  (`~/.cache/smith/llvm/23.1.3-r2`): RTTI and exceptions on, which Alive2 needs, and the patches (`patches/*.patch`: the token hook #11, clang-format for `.smith` #25); the compiler's
  output is byte-identical with and without RTTI and exceptions (only the install path in debug info differs). `smith build
  llvm` also builds, static and from packages pinned by hash, with nothing from Homebrew (Ninja, GMP, MPFR, Meson, CaDiCaL
  and symfpu are among them): Z3 5.3.1, Alive2 (commit 01a5ec4, the newest that builds against 23.1.3) and Bitwuzla 0.9.1
  (`scripts/prove-libs.sh`, `scripts/bitwuzla.sh`). Only the stage0 has no prover. Each function is proved in a process of
  its own. Bitwuzla decides the queries on every core, cheap query first and the full one only if that fails; Z3 only finds a counterexample's values, because on queries
  with many stores through one array Z3 takes minutes where Bitwuzla takes a fraction of a second. Skill:
  smith-contract.
- **A proof is fast, or it is written wrong.** Like a test, each proof has a time budget of about 10 s, and one that
  exceeds it fails. The fix is to write it differently: free inputs instead of random draws, local proofs, and a
  contract per function, so that callers use the contract instead of the body. Proofs are cached by content, and only
  what changed is proved again.
- **Neovim support is this repository** ([docs/neovim.md](docs/neovim.md)): `plugin/smith.lua` is the only Neovim file at the root,
  the code and queries are in `editors/neovim`, and the grammar in `tree-sitter/` extends tree-sitter-c (generated
  `parser.c` committed; the plugin compiles it with `smith cc -shared`). It enables nothing the user did not ask for.
- **smith's libraries are a modern standard library for C, holding only what is proven.**
  - **Joining:** a library joins when two projects need the same thing (game's trigonometry and random's logarithm make
    `<smith/math.h>`), never on speculation. So there are no vectors or tensors until a second project needs one.
  - **What one looks like:**
    - It does one thing and is named for it, so there is no "base" or "utils".
    - It uses standard types: `<stdint.h>`, `size_t`, `float` and `double`, with no aliases.
    - It uses the standard `assert`, and macros only where a function cannot do the job, such as capturing a check's
      expression text.
    - Its names are typed as `<math.h>` names them, with no `_Generic`.
  - **How a project uses one:** it compiles the library from source with its own flags (`smith_library`).
  - **Talking to the OS:** `<smith/io.h>` is the one way a program touches the world outside it (memory, randomness,
    console, files, clock, processes, the network), each platform behind it on its best API (kqueue and Network.framework on macOS).

## Commands

- smith builds itself with its own `build.c`, so the installed smith must have `smith build`. `smith build` builds
  `build/<OS>-<arch>/smith` and the `smith-cc` and `smith-c++` links: with the LLVM install of `smith build llvm` (or
  `-DLLVM=`) it links clang in and compiles in its own process, else it builds the stage0, which runs an external
  clang. `smith test` runs the routing tests against stub compilers (each through `smith cc` and through `smith-cc`),
  then the `smith build` tests with a real clang;
  `smith build compare` runs the same invocations through the clang of the LLVM smith links (`-DREAL=` names another;
  Homebrew's differs in its version string, so the cases with debug info or bitcode differ against it) and through smith.
- `smith check` checks formatting and clang-tidy, with `smith fmt` and `smith lint` of the smith it builds (the
  same configs as game, build.c included), then runs the
  `test` and `compare` targets; it must pass before a change is done. `smith fmt` (no arguments) rewrites the sources,
  and `smith lint` runs clang-tidy over them.
- `smith build install` runs the tests, then copies smith and the `smith-cc` and `smith-c++` links to `$PREFIX/bin`
  (and removes an old `smith++` there) (`-DPREFIX=`,
  default `~/.local/bin`), and a build uses it when CC or CXX names smith. A broken install breaks every build,
  smith's included. The way back needs only a C23 compiler and a clang for build.c, and is also the first build of a
  fresh clone: `mkdir -p build && cc -std=gnu23 smith.c -o build/stage0 && build/stage0 build install -DCC=cc` builds
  and installs the stage0 (set `SMITH_CLANG=/usr/bin/clang` if Homebrew's llvm is absent). To get the full smith, run
  `build/stage0 build llvm` (about 9 minutes once) and then `build/stage0 build install -DCC=cc`, which links clang in.
  `-DCC=` picks the compiler that builds smith; `smith build clean` removes `build/`.
- `smith build llvm` builds LLVM 23.1.3 (clang, clang-tools-extra, lld, compiler-rt; AArch64; static; no dylibs outside
  `/usr/lib` and `/System`) from the pinned source, with a pinned CMake and Python, into
  `~/.cache/smith/llvm/23.1.3-r2/{build,install}` (`scripts/llvm.sh`; it survives `smith build clean`). `check` and a plain
  build never run it. It also keeps clang's own main, with cc1, cc1as and cc1gen-reproducer, as `libclangmain.a` in the
  install, and the mains of clang-format, clang-tidy and clangd (patched copies, see `shim/tool.h`) as `libsmithtools.a`,
  which smith links. Its clang needs `SDKROOT` (it does not ask xcrun), and it cannot compile for x86_64.
- `smith symbolize [--binary <program>] [<log>]` turns the `smith-trace` lines of a failure (a log, a pipe, standard
  input) into the failing line and the call chain; it needs the full smith.
- `smith new [<name>]` writes a ready project into `<name>` (or the current folder), never overwriting: `build.c`,
  `main.c` (a few lines of modern C), `.gitignore`, `.clang-format`, `.clang-tidy`, `AGENTS.md`, `knowledge/` (an index and a first concept), `.agents/skills/<name>-docs` with the `.claude/skills` link, and `.claude/settings.json` (which enables the smith plugin
  and declares its marketplace), then builds once and commits on `main`. No skills are copied. `tests/new.sh` tests it.
- `smith fetch <url>` downloads a package archive into the cache (`$XDG_CACHE_HOME/smith/packages/<hex>/`, else
  `~/.cache/smith/...`) and prints the `.hash = "sha256:…",` line for build.c's `smith_package`. A package is a URL and
  a hash, fetched with smith's own HTTPS client and extracted by smith itself (`.tar`, `.tar.gz`, `.tar.xz`; no other
  process); the hash is the lock (no registry, no lock file). `-DOFFLINE` forbids fetching. macOS only for now.
- **smith reads its archives itself, in its own C** (`runtime/gzip.c`, `xz.c`, `untar.c`): DEFLATE, LZMA2 and tar, no
  vendored library and no zip, which no package uses. Extraction refuses absolute names, `..`, a path through a
  symlink, and a symlink that points outside the package; it keeps file modes and times. Tests: `tests/untar.sh`
  (hostile and damaged archives, and the readers under ASan and UBSan). See [docs/build-c.md](docs/build-c.md).
- **Two caches, one rule:** modules (`<cache>/smith/modules`) and packages (`<cache>/smith/packages`), `<cache>` being
  `$XDG_CACHE_HOME` or `~/.cache`. An entry unused for 31 days is removed, checked at most once a week: clang does it for
  modules (smith passes `-fmodules-prune-interval=604800 -fmodules-prune-after=2678400`), `smith_package` for packages
  (a `<hex>.used` stamp per package and `prune.timestamp`). Both are keyed by hash, so a removed entry is fetched or built
  again. `smith cache` shows each cache's path, entries and size (`-v` lists the entries and their last use);
  `smith cache clean [modules|packages]` empties them, and refuses while a `smith build` runs (builds hold a shared flock
  on `<cache>/smith/lock`). Tests: `tests/cache.sh`, with caches of their own. See [docs/build-c.md](docs/build-c.md).
- `SMITH_CLANG`, `SMITH_CLANGXX` (an external clang, instead of the one inside smith) and `SMITH_METAL` pick the compilers, `SMITH_VERBOSE=1` prints the command, and
  `smith cc --smith-which <args>` (or `smith-cc --smith-which <args>`) prints the choice without running it. `SMITH_NINJA=<ninja>` makes `smith build` run its
  manifest with that ninja instead of smith's own executor.

## How to work

- Load the [smith-development skill](.agents/skills/smith-development/SKILL.md) for any change to smith itself.
- Load the [smith skill](.agents/skills/smith/SKILL.md), the map, when using smith; it names the small skills for the compiler ([smith-cc](.agents/skills/smith-cc/SKILL.md)), `run` and `test` ([smith-run-test](.agents/skills/smith-run-test/SKILL.md)) and `check`, `fmt`, `lint` and `lsp` ([smith-check](.agents/skills/smith-check/SKILL.md)). They ship in the smith plugin, from `.agents/skills`; no project carries a copy.
- Load the [smith-build skill](.agents/skills/smith-build/SKILL.md) to run, write or edit a build.c, smith's own included. It ships in the smith plugin, from `.agents/skills`; no project carries a copy.
- Run the real thing and report numbers. Say plainly what was not tested.
- `~/Documents/checkers`, `~/Documents/virtual-cell/learner` and `~/Documents/game` are the test beds: read them, build
  them in scratch copies, and change their sources only with the owner. When smith changes one, leave its agents a
  note on what changed and why.
- **The plugin's skills describe everything smith does today, and nothing it doesn't do yet.** Every command, build.c
  call and library has a skill that another project's agent can learn it from. Plans go in GitHub issues, never in a
  skill. A change that adds or changes something updates its skill in the same commit.
- Small Conventional Commits.
- Hand work to subagents whenever possible, on Sonnet 5.5 at medium reasoning effort.

## checkers (`~/Documents/checkers`)

A small checkers game, men only, kept small enough to reason about and to prove correct. Its tests found these bugs,
which are fixed at their source (checkers' history up to 2d6bbfa still has them, as cases for `smith prove`):
- `get_user_move` tested `from_char <= 'H'` twice and never `to_char`, so column I read `cells[row][8]`.
- At end of input `get_user_move` looped forever: `scanf` returned `EOF` and nothing ended the loop.
- `get_valid_diagonals` and `get_valid_jumps` missed legal moves (`else if`; `col > 1`, `row > 1`, `col > 2` off by
  one): 419,348 over 200,000 random boards, and no illegal move generated.
- `apply_follow_up_jumps` recursed. It is now a loop bounded by three jumps a turn (men only move forward, two rows a
  jump), the bound CBMC proved.

## Known gaps

- A response file (`@file`) is opaque, so a `.metal` input inside one goes to clang.
- Apple's `metal` output is not reproducible (two identical builds of learner's shaders differ), so compare shaders by
  the command smith runs, not by bytes.
- Why upstream's ASan is slow on macOS (about 80 ms a process against 18 ms for Apple's) is not known: the time is kernel time, in both `fork` and `posix_spawn`, which
  only root can trace.
