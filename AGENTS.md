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
  - **Sanitized processes start as fast as Apple's.** Upstream compiler-rt maps AddressSanitizer's 14 TB of shadow
    without XNU's fast-mmap flag on arm64 (it passes `VM_FLAGS_4GB_CHUNK` only on x86_64, and its 2 GB threshold
    `2 << 30` overflows `int`), so the kernel paid per TB at every start, `fork` and exit: about 38 ms a process (80
    under load) and 40 ms a `fork`. `patches/compiler-rt-xnu-fast-mmap.patch` (two lines, what Apple's clang does) fixes
    both: an empty sanitized program starts in 9.0 ms against 8.3 ms for Apple's (40 runs, medians), and a full
    `smith check` spends about 117 s of kernel time instead of about 400.
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
  project with them** (`runtime/docs.c`; skill smith-docs); the concept types and required fields are `types:` and
  `requires:` in the frontmatter of `knowledge/index.md`, and the command line's `--type` and `--require` replace them.
- **The verbs have fixed meanings, and a project's own tasks run with `smith build <task>`** (issue #40;
  `runtime/verbs.c`, docs/build-c.md). Each verb acts on what the project already declares, and smith keeps no list of
  its own: `smith fmt` formats every C, C++, Objective-C and `.smith` file git sees to `.clang-format` (what
  `.clang-format-ignore` names is left alone); `smith lint` is clang-tidy on the project's own sources among the build's
  compiles, in parallel; `smith docs` checks AGENTS.md, `knowledge/` and `.agents/skills/` (the concept types and
  required fields are declared in `knowledge/index.md`'s frontmatter); `smith prove` proves the contracts of the
  C sources of each program the build links; `smith test` runs what `smith_tests` declares (its arguments go to the runner); `smith run` runs the program
  that `smith_default` names, when it names exactly one (its arguments go to the program); `smith check` runs fmt (the dry
  run), docs, lint, test and prove, reports every part and fails if any did. With arguments `fmt`, `lint` and `prove`
  are clang-format, clang-tidy and the prover, as before. `smith_task` and `smith_alias` refuse a name that is a verb. A
  run of build.c writes `.smith/build.info` and `.smith/compile_commands.json`, which the verbs read, and the manifest's
  targets `smith_needs` and `smith_tests`. `smith build <verb>` is refused with a pointer to the verb. `smith prove` is
  the prover with arguments; `smith dbg` is lldb.
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
  name lookup as the lowering's questions. A second patch (`patches/clang-debug-name.patch`, about 60 lines, a process-wide
  hook in `CGDebugInfo`) lets the debugger show a module's names as written: `geometry::box::area` for a function or variable
  (its `DW_AT_name`), `Box` in namespaces `geometry::box` for a type; plain C debug info is byte-identical
  ([docs/debugging.md](docs/debugging.md)). Modules use `::` (`module a::b;`, `x::name`, `c::printf`), never dots. The
  `.smith` extension turns it on (`smith cc x.smith` is `-x c -std=gnu2y x.smith`: `.smith` is smith's own language and
  compiles as C2y, the standard `smith new` uses, so `auto`, `::` and `_Countof` work, unless the caller names a
  `-std=`, which wins); plain C in a `.smith` file means what it means in a `.c` file (`tests/smith-lang-c.sh`).
  `smith cc main.smith foo.smith` just works: an `import foo` that finds no `foo.h` on the include path reads the
  module's source (among the command's `.smith` files, else at the path its name implies under the importing module's
  root) and makes its interface in memory, writing nothing; `smith build` still writes the headers and they win. The
  sized types (`u32`, `f64`, ...) are typedefs, made when a file first uses one it does not declare itself, so a
  diagnostic reads `'i32' (aka 'int')`; a generated C header still says the standard C types. The lowering reports a C
  keyword used as a name (`i32 double(i32 x)`) itself, at the name. A smith whose clang lacks the hook, a stage0 included, refuses a `.smith`
  input; it can still write a module's header (`smith cc --smith-header`). The compiler's helpers are under `smith cc`
  (`--smith-header`, `--smith-lower`, `--smith-which`), since smith's top level is only verbs. `smith build` builds the
  modules itself: a header step per module, and every `.smith` compile waits for all of them (docs/build-c.md).
- **Generic functions are lowered, not templates** ([docs/smith-lang.md](docs/smith-lang.md), issue #15; skill smith-lang-generics).
  Every compile-time parameter is marked `constexpr`, a type parameter included: `export T *push(Arena *a, constexpr type T)`,
  called as `arena::push(&a, Point)` or, with the types left out, inferred from the arguments. The lowering records the
  generic's tokens (no clang patch), writes one weak, hidden instance per canonical list of types at the end of the file,
  and chooses the branch of each `if constexpr` for it, so the parser only ever sees ordinary functions; an error in an
  instance is reported at the generic's source with a note at the call. An instance's code is a copy of its generic's source
  file under the same name, so the debugger shows `pick::larger<i32>` at the generic's own lines. Value parameters and
  functions that return `type` are the next extension.
- **`smith lsp` lowers `.smith` files too.** `smith_clangd_main` installs the same token hook, so a parse of a `.smith`
  file in clangd is lowered as a compile's is and positions are those of the source; no LLVM change. The lowering is
  C with static tables, so a hook holds it from its making to the end of its file and clangd's `.smith` parses take
  turns. clangd does not know the extension, so `shim/clangd.cpp` defines `clang::createInvocation` (the upstream function)
  with the one rule of `smith cc` for `.smith` inputs (`smith_lang_command`, `runtime/lang_command.c`): `-x c`, and
  `-std=gnu2y` unless a standard is named, for every command clangd uses, a folder with no compile database included
  (a single file or a few modules need no build.c); no LLVM change. Names are shown as written (`box::area`, `area` in
  the module), never guessed from underscores: the lowering keeps a table of the names it made, a diagnostic consumer in
  the hook rewrites messages, notes and fix-its (`smith cc` too) and sends a note into a generated header to the
  declaration in the module's `.smith`, and a layer between the editor and clangd (`shim/lsp.cpp`, in process, pipes on
  clangd's stdin and stdout; `SMITH_LSP_RAW=1` turns it off) rewrites hover, symbols, completion (`box::` offers the
  module's exports), signature help and hints. A module's header has a `// smith-names:` line of its exports, which depends
  on them only, so an edit to a body does not rebuild importers. A `c::name` that is no macro stays a plain token in the
  main file (a reinjected one is not in clangd's token buffer, so hover and go-to-definition found nothing there), and
  hover leaves out the `asm("_foo::f")` label of a module's name; an importer still reads a module's types as the header
  writes them (`int32_t`), since a generated header says the standard C types (`tests/lsp.sh`, [docs/smith-lang.md](docs/smith-lang.md)).
- **`smith lint` lints `.smith` files as written** (`shim/tidy.cpp`; no LLVM change). clang-tidy builds each file's
  command with the driver, not `createInvocation`, so `shim/tidy.cpp` carries `ArgumentsAdjusters.cpp` whole with
  `smith_lang_command` called in `getStripPluginsAdjuster` (the last adjuster; same rule as `smith cc`), and installs the
  token hook. A check reports from an engine of its own, so the hook puts the same consumer in front of it
  (`smith::presentThrough`): names, notes and fix-its as written, a prototype's lowering copy moved to the definition's
  name, a finding in a generated or in-memory interface dropped (the module's own file is linted as itself).
  `readability-identifier-naming` judges the written name: a wrapper module, registered after clang-tidy's, replaces that
  one check with `SmithNaming` (it holds the final real check, whose private decisions it reaches by an explicit
  instantiation) which gives the declaration its member name from the lowering's table for the one question. Sound
  because the table is the lowering's own, never underscores guessed; the rename is undone before anything else reads
  it; a `.c` file's names are never in the table. Other checks see the AST as it is, so one that compares a name's
  spelling still sees the lowered one (`tests/lint-smith.sh`, [docs/smith-lang.md](docs/smith-lang.md)).
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
  (`~/.cache/smith/llvm/23.1.3-r2`): RTTI and exceptions on, which Alive2 needs, and the patches (`patches/*.patch`: the token hook #11, clang-format for `.smith` #25, debug names #27); the compiler's
  output is byte-identical with and without RTTI and exceptions (only the install path in debug info differs). `smith build
  llvm` also builds, static and from packages pinned by hash, with nothing from Homebrew (Ninja, GMP, MPFR, Meson, CaDiCaL
  and symfpu are among them): Z3 5.3.1, Alive2 (commit 01a5ec4, the newest that builds against 23.1.3) and Bitwuzla 0.9.1
  (`scripts/prove-libs.sh`, `scripts/bitwuzla.sh`). Only the stage0 has no prover. Each function is proved in a process of
  its own, which ends at its budget (SIGALRM) and when its parent dies. Bitwuzla decides the queries on every core, cheap query first and the full one only if that fails; Z3 only finds a counterexample's values, because on queries
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
  - **C or `.smith`, one registry:** a library is its header (`include/smith/<name>.h`) and its source, `lib/<name>.c`
    or a `.smith` module `lib/smith/<name>.smith` (`module smith::<name>;`), one row of `smith_libraries` in
    `smith.c`. A `.smith` library's header is generated and committed (the stage0 has no hook and embeds it as
    bytes), `smith build headers` regenerates it and `check-headers` (in `check`) fails on a difference; its C API
    (names, symbols via `[[smith::c_name]]`, doc comments) stays what it was. `text` is the first (issue #39).
  - **Talking to the OS:** `<smith/io.h>` is the one way a program touches the world outside it (memory, randomness,
    console, files, clock, processes, the network), each platform behind it on its best API (kqueue and Network.framework on macOS).

## Commands

- smith builds itself with its own `build.c`, so the installed smith must have `smith build`. `smith build` builds
  `build/<OS>-<arch>/smith` and the `smith-cc` and `smith-c++` links: with the LLVM install of `smith build llvm` (or
  `-DLLVM=`) it links clang in and compiles in its own process, else it builds the stage0, which runs an external
  clang. `smith test` runs the routing tests against stub compilers (each through `smith cc` and through `smith-cc`),
  then the `smith build` tests with a real clang; each `tests/*.sh` is a task of its own (`test-<name>`, with `.test`, which `smith test` runs),
  so they run side by side (skill smith-development);
  `smith build test-compare` (one of smith's tests, so `smith test` and `smith check` run it) runs the same invocations through the clang of the LLVM smith links (`-DREAL=` names another;
  Homebrew's differs in its version string, so the cases with debug info or bitcode differ against it) and through smith.
- `smith check` is the standard one here too: `smith fmt` (the dry run) over every file git sees, `smith docs`, `smith
  lint` (clang-tidy over the compiles of `build.c`, which also declares the libraries' and tests' sources with
  `smith_compile_command`; `shim/.clang-tidy` is the C++ glue's own rules), `smith test` (the `test-<name>` tasks, `test-compare`
  among them) and `smith prove`; it must pass before a change is done. `smith fmt` (no arguments) rewrites the sources.
- `smith build install` runs the tests, then copies smith and the `smith-cc` and `smith-c++` links to `$PREFIX/bin`
  (and removes an old `smith++` there) (`-DPREFIX=`,
  default `~/.local/bin`), and a build uses it when CC or CXX names smith. A broken install breaks every build,
  smith's included. The way back needs only a C23 compiler and a clang for build.c, and is also the first build of a
  fresh clone: `mkdir -p build && cc -std=gnu23 smith.c -o build/stage0 && build/stage0 build install -DCC=cc` builds
  and installs the stage0 (set `SMITH_CLANG=/usr/bin/clang` if Homebrew's llvm is absent). The `.smith` sources smith's own build compiles are built from their
  committed lowering in `bootstrap/` (`smith build bootstrap` writes it, `check-bootstrap` in `check` keeps it current), so that first build
  needs no knowledge of `.smith` and the protocol generator builds with the stage0. To get the full smith, run
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
  `main.c` and `primes.c` (a few lines of modern C, split so that a test can call them), a test in `tests/` that `smith test` runs (`smith_tests`), `.gitignore`, `.clang-format`, `.clang-tidy`, `AGENTS.md`, `knowledge/` (an index and a first concept), `.agents/skills/<name>-docs` with the `.claude/skills` link, and `.claude/settings.json` (which enables the smith plugin
  and declares its marketplace), then builds once and commits on `main`. No skills are copied. `tests/new.sh` tests it.
- `smith fetch <url>` downloads a package archive into the cache (`$XDG_CACHE_HOME/smith/packages/<hex>/`, else
  `~/.cache/smith/...`) and prints the `.hash = "sha256:…",` line for build.c's `smith_package`. A package is a URL and
  a hash, fetched with smith's own HTTPS client and extracted by smith itself (`.tar`, `.tar.gz`, `.tar.xz`; no other
  process); the hash is the lock (no registry, no lock file). `-DOFFLINE` forbids fetching. macOS only for now.
- **smith reads its archives itself, in its own C** (`runtime/gzip.c`, `xz.c`, `untar.c`): DEFLATE, LZMA2 and tar, no
  vendored library and no zip, which no package uses. Extraction refuses absolute names, `..`, a path through a
  symlink, and a symlink that points outside the package; it keeps file modes and times. Tests: `tests/untar.sh`
  (hostile and damaged archives, and the readers under ASan and UBSan). See [docs/build-c.md](docs/build-c.md).
- **The protocol code of `smith mcp` is generated, never written by hand** (issue #22): `smith build protocol` runs `scripts/protocol.c` on DAP's and MCP's JSON
  Schemas, each a package pinned by hash in build.c, and writes `runtime/protocol/{dap,mcp}.[ch]` and their tests `tests/protocol/test_protocol_*.c` on
  `<smith/json.h>`. The output is committed (a fresh clone and the stage0 need no network); `smith check` runs `check-protocol`, which fails when a
  committed file is not what the generator writes now (`-DOFFLINE` skips a package that is not cached); `tests/protocol.sh` builds and runs the tests under
  the sanitizers. See [docs/protocol.md](docs/protocol.md).
- **`smith mcp` is the debugger as an MCP server over stdio** (issue #22; `runtime/mcp*.c`, [docs/mcp.md](docs/mcp.md), skill smith-debug): eleven tools
  (`launch`, `breakpoint`, `continue`, the steps, `pause`, `stack`, `variables`, `evaluate`, `stop`) over `smith dap` children, on `<smith/io.h>` and the
  generated protocol types, no malloc. Only the full smith has it (`-DSMITH_MCP`); the smith plugin declares it (`mcpServers`). `tests/mcp.sh` runs it on the
  fake io and end to end.
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
