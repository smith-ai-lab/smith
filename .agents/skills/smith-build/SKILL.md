---
name: smith-build
description: "Running, writing and editing a project's build.c with smith build. Load when a project has a build.c at its root, when running smith build, on any message that starts with smith: build.c:, when something under .smith/ comes up, or when replacing a Makefile with build.c."
---

# Build with smith build

This skill ships in the smith plugin, from `.agents/skills/smith-build/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there; projects get the update through the plugin.

`build.c` describes the build in C, explicitly and with no defaults. `smith build` compiles it, runs it, and runs the manifest it writes (a ninja-language file) with its own executor; no ninja is needed. The API is `.smith/include/smith/build.h` and its implementation is `.smith/runtime.c`. Every `smith build` writes both, and they match the installed smith exactly, so read them first. The source's home is https://github.com/adam-r-kowalski/smith (`include/smith/build.h`, `runtime/build_runtime.c`), and the design and its reasons are in `docs/build-c.md` there. The repository is private, so read it with `gh`, for example `gh api repos/adam-r-kowalski/smith/contents/docs/build-c.md -H 'Accept: application/vnd.github.raw'`.

## Run it

- `smith build` builds the defaults. `smith build <name>...` builds those targets or runs those tasks. Run it from the folder that holds `build.c`.
- **The verbs run a project's own tasks.** `smith run`, `smith test`, `smith check`, and `smith fmt` and `smith lint` with no arguments each build and run the task or alias of the same name in build.c; if build.c defines none, the message says to add `smith_task(b, "<verb>", ...)`. Name them as you would any task: `run` is the program (`.command = smith_cat(b, app.outputs, smith_args(b), nullptr)`), `test` is what `smith_tests` adds, `check` an alias of the checks that must pass (format, lint, tests), `fmt` the task that rewrites the sources (`smith fmt -i <files>`) and `lint` the task that runs clang-tidy (`smith lint --quiet -p . <files>`, reading the flags from compile_commands.json). `-D` options work on a verb as on `smith build`; every other argument is the verb's, given to build.c by `smith_args(b)` (`--` ends the `-D` options, so `smith run -- -Dx` passes `-Dx` to the program). `smith build run` and the other verb names are refused, so there is one way to run each. `smith dbg` says it is not available yet.
- `-Dname` and `-Dname=value` set build.c's options for this call only. An option build.c does not declare is an error that lists the declared ones.
- Every other argument is the executor's, and works as ninja's did: `-j N` (default: cores + 2), `-n`, `-v`, `-k N` (keep going until N jobs fail; `-k 0` never stops), `--quiet`, and `-t <tool>`: `targets [all]`, `clean [-g] [targets]` (removes every declared output), `compdb [rules]` (prints compile commands), `commands <targets>`, `deps [targets]` (what each output read last time), `cleandead`. `-d explain` says why each step is dirty. `-f` and `-C` are refused; `-l`, `-w` and other `-d` modes are ignored. A job prints its output whole when it ends, Ctrl-C stops the jobs and exits 130.
- Each call compiles `build.c` into `.smith/program` when build.c, a file it includes or smith changed, runs it, which writes `.smith/build.ninja` when its bytes changed, and then runs that manifest. So globs, environment variables and options are read fresh every time. A no-op takes a few milliseconds.
- `.smith/` is smith's own folder and ignores itself in git. Never edit it. `rm -rf .smith` is always safe; the next build recompiles build.c and reruns every step.
- `SMITH_VERBOSE=1` prints the commands smith runs. `SMITH_NINJA=<path to ninja>` runs the manifest with that ninja instead of smith's executor (a fallback; ninja keeps its own logs, so each tool rebuilds what the other never built).

## Write it

```c
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep app = {
        .command = (const char *[]){"smith-cc", "-std=gnu23", "-g", "$in", "-o", "$out", nullptr},
        .inputs = (const char *[]){"app.c", nullptr},
        .outputs = (const char *[]){"app", nullptr},
    };
    smith_step(b, &app);
    SmithStep run = {.command = (const char *[]){"./app", nullptr}, .inputs = app.outputs, .pool = "console"};
    smith_task(b, "run", &run);
    smith_default(b, app.outputs);
}
```

- Declare a step, then pass its address. Afterwards its lists point at smith's own copies, so `app.outputs` can feed later steps.
- A list ends in `nullptr` and is never `nullptr` itself; the empty list is `(const char *[]){nullptr}`. `smith_cat(b, a, c, nullptr)` joins lists.
- A compound literal `(const char *[]){...}` lives only until the end of the block that holds it. `smith_list` is the list that lives on. Every smith call copies what it is given, so pass a literal straight to a call, and keep what a call returns (`step.outputs`, `smith_cat(...)`), which lives until the build ends. Never return a literal from a function, or store one in a struct that outlives its block: the game port did, and it crashed in a way ASan did not catch.
- `.command` is an argv, run with no shell. The whole words `$in` and `$out` become the step's inputs and outputs; every other word is passed exactly as written. For pipes, redirections or `&&`, write `{"sh", "-c", "<script>", ...}`. The script may span lines (`\n` in the C string): smith writes such a command to `.smith/command/<hash>` and runs it with `sh`, so editing the script reruns the step. A newline in a path, `.description`, `.depfile` or pool name is an error.
- The fields: `.inputs` ($in; a change reruns the step), `.implicit_inputs` (a change reruns it, not on the command line), `.order_only_inputs` (built first, a change does not rerun it), `.outputs`, `.depfile` (the file `-MMD -MF` writes), `.description`, `.pool` (`"console"` gives the terminal), `.restat` (an unchanged output does not rerun what depends on it), `.always` (runs every build).
- A file the build generates and a compile reads with `#include` or `#embed` goes in that compile's `.implicit_inputs`; the depfile alone misses it on a clean build.
- `smith_task(b, name, &step)` is a target run by name, every time it is asked for, like make's `.PHONY`. A task has no outputs, `.restat` or `.always`. A step with no outputs instead reruns only when its inputs change.
- A task or alias name may hold `/` (`check-alone/src/base/arena.c`), but not start with `/` or `-`, end with `/`, or hold an empty, `.` or `..` component.
- A step, task or alias declared twice with every field equal is accepted and changes nothing (`*step` becomes the earlier copy), so two groups can share an object file. Any difference is an error that names the output and the first field that differs.
- `smith_alias(b, name, targets)` names a group. `smith_default(b, targets)` is required; an empty list builds nothing. `smith_pool(b, name, depth)` limits a set of steps.
- Options: `smith_option(b, name, description)` for `-Dname`, `smith_option_value(b, name, fallback, description)` for `-Dname=value`. A make `VAR ?= x` is `smith_option_value(b, "VAR", smith_env(b, "VAR", "x"), "...")`. Calling either again with the same name and kind returns the same value (the description may be `nullptr`), so a `smith_defer` callback can read the options `build()` read; the other kind is an error.
- The outside world: `smith_env`, `smith_glob(b, patterns)` (a list of patterns, like `$(sort $(wildcard a b c))`: the union, sorted, each name once; a `**` component matches zero or more folders, so `src/**/*.c` finds `src/a.c` and `src/x/y/b.c`; hidden entries match only a component that starts with `.`), `smith_capture` (runs an argv with no shell and returns its output; a failing program is an error) and `smith_capture_or(b, argv, fallback)` (the fallback when the program cannot start, exits non-zero or is killed; its stderr is discarded). `smith_capture` runs on every `smith build`, so a slow command that only some targets need belongs inside those steps' commands, as `$(...)` in `sh -c`.
- `smith_write(b, path, text)` writes a file when build.c runs, through a temporary file and a rename and only when its bytes differ, so its mtime moves only when the text does. It makes missing folders; the path is relative, with no `..` and nothing under `.smith/`. A step that reads the file lists it as an input. `-t clean` leaves it.
- Reading generated files: a file a step makes is read with `smith_lines` inside `more`, the function given to `smith_defer(b, needs, more)`, never in `build()`. `needs` are the step outputs (or sources) the file comes from; once they exist, `more` runs and declares the steps that the file's lines name. `smith_lines(b, path)` returns one entry per line without the newline, and is an error for a missing file. `more` may call `smith_defer` again (limit 256 calls); `smith_default` may be called there.
- `smith_package(b, &(SmithPackage){.url = "https://...", .hash = "sha256:..."})` is the absolute path of an archive's extracted contents, in the cache `$XDG_CACHE_HOME/smith/packages/<hex>/` (or `~/.cache/...`). The first call fetches (smith's own HTTPS client: redirects to https only, the system's certificate checks), checks the SHA-256 and extracts in smith itself (`.tar`, `.tar.gz`, `.tar.xz`; a zip is refused; absolute names, `..`, a path through a symlink and a symlink that points outside the package are errors; file modes and times are kept); later calls only look it up, so builds work offline. The hash is the lock; there is no registry or lock file. Get the line with `smith fetch <url>`, which prints `.hash = "sha256:...",`. The packages and the modules are smith's two caches, under `$XDG_CACHE_HOME/smith/`, with one rule: an entry unused for 31 days is removed, checked at most once a week (packages are pruned by `smith_package` itself, with a `<hex>.used` stamp each). Both are keyed by hash, so a removed entry is fetched or built again. `smith cache` shows each cache's path, entries and size (`-v` lists the entries and their last use), and `smith cache clean [modules|packages]` empties them; it refuses while a `smith build` is running. A wrong hash is an error with both hashes; `-DOFFLINE` turns a missing package into an error that names its URL. Use the path in steps, `-I` flags and objects. macOS only for now.
- `smith_library(b, "arena")` is the path of a smith library's source (`.smith/lib/arena.c`); smith writes the sources to `.smith/lib/` and the headers to `.smith/include/smith/` on every `smith build`. The project compiles the source itself with its own flags, and adds `-I .smith/include` to its compiles to find `<smith/arena.h>`: `SmithObject arena = {.compiler = "smith-cc", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = smith_cat(b, flags, smith_list(b, "-I", ".smith/include", nullptr), nullptr)}; smith_object(b, &arena);`. Nothing is linked unless build.c asks. An unknown name is a `smith: build.c:` error listing the known ones. "random" and "io" use the arena (and "io" random), so a project compiles what it uses. See the smith-arena, smith-random and smith-io skills.
- **`.smith` sources** go in `smith_object` like any source (`.source = "src/game/replay.smith"`, `.compiler = "smith-cc"`; load `smith-lang`). A module (`module a::b;` first) also gets a header step (`smith-cc --smith-header src/a/b.smith -o .smith/include/a/b.h`, written only when its bytes change), and every `.smith` compile gets `-I .smith/include` and waits for all the headers and for a check that fails, naming the cycle, on an import cycle (`smith: import cycle: a::x -> a::y -> a::x`). So an edit to a module's body recompiles that module alone, and an edit to its exports its importers. The module's path must end in `a/b.smith` (checked when build.c runs). A C or C++ file that includes a module's header (`#include "a/b.h"`) needs `-I .smith/include` in its flags and `.smith/include/a/b.h` in its `.implicit_inputs`. compile_commands.json lists `.smith` sources with `-I .smith/include`. A stage0 writes headers but cannot compile a `.smith` source.
- `smith_modules(b, &(SmithModules){.folders = (const char *[]){"src", nullptr}, .textual = ...})` turns on Clang modules for the `.c` and `.m` objects declared after it (call it first; never `.metal` or C++): every `#include` of a header under `.folders`, of smith's (`.smith/include/smith/`) or of the SDK becomes an import, parsed once and cached. Each header is its own module, so editing one rebuilds only what imports it. smith writes the maps to `.smith/modules/` and clang keeps its cache in `$XDG_CACHE_HOME/smith/modules` (else `~/.cache/smith/modules`), shared by every project and worktree so the SDK's modules are built once per machine, and left by a clean of `build/`. Flags added: `-fmodules -fmodules-cache-path=... -fmodules-prune-interval=604800 -fmodules-prune-after=2678400 -fmodule-map-file=...`; compile_commands.json leaves them out because clangd with `--query-driver` cannot build the SDK's modules. A header that only works after another include fails with clang's `While building module` error: make it include what it uses, or list it in `.textual` (as `smith_glob` spells it) so it stays a plain `#include`. `smith new` turns it on with no folders; add yours. Explicit modules are not implemented.
- `smith_tests(b, &tests)` builds a runner from the test files (`.sources`) and the code under test (`.objects`, which must include smith's arena and random) and adds the task `test`, which `smith test` runs: its arguments are the runner's own options (`smith test --test name --seed 7 --jobs 1`). `.flags` are for the test files and smith's test library, `.link_flags` for the link, `.output` is the runner (`build/tests`), `.name` the task (`test`), `.compiler` defaults to `"smith-cc"`. Tests are found by name: lines that are exactly `void test_<name>(void) {` or `void test_<name>(SmithKey key) {`. See the smith-test skill.
- Helpers: `smith_fmt` (printf), `smith_map(b, items, "out/%s.o")`, `smith_when(b, condition, items)`, `smith_replace_extension(b, "src/a.c", ".o")`, `smith_join(b, items, sep)` (text from a list, `""` when empty: `smith_join(b, smith_map(b, files, "#include \"%s\"\n"), "")` is a unity file) and `smith_list(b, "a", "b", nullptr)` (a list in the arena that never dangles, so keep it or return it; `smith_list(b, nullptr)` is the empty list).
- `b->os` and `b->arch` are exactly what `uname -s` and `uname -m` print: `"Darwin"`, `"arm64"`.
- C functions as steps: `.run = fn` (with `.command` empty, and `.args` for the function) makes a step or task call `int fn(SmithRun *run)` while the build runs, for checks that read build outputs. It returns 0 for success, anything else fails the step with that status. `run->inputs`, `run->outputs`, `run->args` are the step's lists and `run->b` works with every helper that needs no `build()` state. Like a command, a function writes the outputs it declares; a check declares none, and smith's stamp records that it passed. Editing build.c reruns every `.run` step.
- **A function's data is its step's lists, nothing else.** The step's command is `.smith/program --run <function> --input ... --output ... --arg ...`; `build()` and the `smith_defer` callbacks do not run for it, and the `-D` options are not passed. Put what the function needs in `.inputs`, `.outputs` and `.args` (an option's value goes in `.args`: `.args = smith_list(b, smith_option_value(b, "who", "nobody", "a name"), nullptr)`). A `.run` step costs about the cost of starting a process.
- Inside the function, `smith_run_lines(run, argv)` runs a program with no shell and returns its stdout lines, or prints `smith: <argv[0]> failed (status N)` and returns `nullptr`; `smith_run_status(run, argv, &lines)` returns the status (127 when it cannot start) and prints nothing. `smith_run_program(run, argv, environment)` runs a program with no shell and its stdout and stderr going straight to the terminal; `environment` is a list of `NAME=value` entries (or `nullptr`) added to the current environment, and it returns the status the same way (127 when it cannot start). All three flush stdout first, so the function's own printing comes first. `smith_write` plus `.restat` stops what follows from rerunning when the bytes are the same.
- `smith_run_combined(run, argv, &lines)` is `smith_run_status` with the program's stderr in the same stream as its stdout, in the order written (`2>&1`), for comparing a program's whole output with an expected text.
- A failed `.run` step prints `smith: <description>: failed (status N)` as its last line (its first output when it has no `.description`), so give a check a `.description`. `smith_run_depend(run, path)` records a file the function read, which it learned at run time; the step must have a `.depfile`, which smith writes when the function returns, so a change to that file reruns the step.
- `.stamp = "build/check/a.stamp"` is the output of a step whose result is only that it passed: smith touches it when the command succeeds or the function returns 0 (no `sh -c '... && touch'`), and `.depfile` may name it as its target (`-MT`). It cannot be combined with `.outputs`, and a task cannot have one. A syntax check is `{.command = {"smith-cc", flags..., "-fsyntax-only", "-MMD", "-MF", "x.d", "-MT", "x.stamp", "x.c"}, .inputs = {"x.c"}, .stamp = "x.stamp", .depfile = "x.d"}`.
- Not implemented yet: `.content_hash`; it fails with "not implemented yet". If the build needs it, tell the owner.

```c
static int check_nm(SmithRun *run) { // fail if an object uses a symbol outside .args
    const char *const *used = smith_run_lines(run, (const char *[]){"nm", "-uj", run->inputs[0], nullptr});
    if (used == nullptr) return 1;
    int failed = 0;
    for (const char *const *s = used; *s; s++) {
        if (strcmp(*s, run->args[0]) != 0) { fprintf(stderr, "%s uses %s\n", run->inputs[0], *s); failed = 1; }
    }
    return failed;
}
// in build(): SmithStep check = {.run = check_nm, .args = (const char *[]){"_puts", nullptr},
//     .inputs = obj.outputs, .outputs = (const char *[]){"build/check.stamp", nullptr}}; smith_step(b, &check);
```

## High level

- `smith_object(b, &object)` is one compile (`<compiler> <flags> -MMD -MF <output>.d -c <source> -o <output>`, with that depfile), `smith_program(b, &program)` links objects (`<linker> <flags> <objects> <libraries> -o <output>`), and `smith_compile_commands(b, path)` writes compile_commands.json from them, plus an entry for build.c. They emit ordinary steps; `smith build -v` shows the commands. `smith_compile_command(b, compiler, source, flags)` adds an entry with no step for a file the build never compiles on its own (a unity-build source), so the editor knows its flags; call it before `smith_compile_commands`, and a file that already has an entry is skipped. Pass each object to `smith_object` before the program that links it. Libraries that are files rebuild the program; `-framework X` is not a file. A change of compiler rebuilds every object and program it made, and a build with the same compiler does nothing: each depends on `.smith/compiler/<hash>`, which names the compiler (smith is the clang it runs: for the clang inside it the LLVM version and a hash of what is linked, so a smith rebuilt with the same LLVM rebuilds nothing; for `SMITH_CLANG`, that clang's file).
- clangd reads it as it is: use `smith lsp` (clangd, from the LLVM smith compiles with; see the smith-check skill for editor setup) with no `--query-driver`. With `--query-driver` clangd cannot build the SDK's modules, and without it the compile command naming `smith-cc` works, module flags included. lldb stays Apple's.

```c
void build(SmithBuild *b) {
    SmithObject main = {.compiler = "smith-cc", .source = "main.c", .output = "build/main.o",
                        .flags = (const char *[]){"-std=gnu23", "-g", nullptr}};
    smith_object(b, &main);
    SmithProgram app = {.linker = "smith-cc", .output = "build/app",
                        .objects = (const SmithObject *[]){&main, nullptr},
                        .libraries = (const char *[]){"-lm", nullptr}};
    smith_program(b, &app);
    smith_compile_commands(b, "compile_commands.json"); // last
    smith_default(b, app.step.outputs);
}
```

## Start a project

- `smith new <name>` (or `smith new` in the folder) creates a project with build.c, main.c, the rules (`.clang-format`, `.clang-tidy`, `.gitignore`), `.claude/settings.json`, which enables the smith plugin (it carries these skills), and its docs (`AGENTS.md`, `knowledge/` with a first concept, the project's own skill `.agents/skills/<name>-docs`, the `.claude/skills` link, and a `docs` step in build.c that `smith check` runs: see `smith-docs`), then builds once and commits on `main`. It never overwrites: a file that exists is kept. The project builds three ways, each into `build/<name>/<project>`: `smith build` is debug (`-O0`, sanitizers, assertions on), `smith build release` is `-O2` with assertions on, and `smith build fast` is `-O2 -DNDEBUG`, assertions off. Its sources are C2y (`-std=gnu2y`, for `_Countof`), a draft standard that Apple clang and upstream clang 21 or later accept; the project's build.c itself is still compiled by `smith build` as C23.

## Replace a Makefile

- Keep every command word for word and every output path the same; a linked binary records its objects' paths.
- Build with make and with `smith build` from clean in the same folder, and compare the outputs before deleting the Makefile. Binaries built with `-g` in one compile-and-link step differ between two make builds too (a temporary object's name), so compare their machine code (`otool -tV` or `objdump -d`).
- make runs one recipe at a time without `-j`; smith build runs independent steps in parallel. `smith build -j1` gives make's order.

## When something goes wrong

- `smith: build.c: <step or task>: <message>` is a mistake in build.c. Fix build.c.
- A compiler error in build.c means build.c does not compile; the failed step prints clang's message.
- `smith: error: ...` is the executor's own, for example an input that no step makes and that does not exist, a dependency cycle, or a manifest it cannot read.
- `smith: no build.c in this folder`: run from the project's root. `smith: cannot find ninja` appears only with `SMITH_NINJA` set to something that is not ninja.
- If smith build itself seems wrong, or smith is missing or broken, stop. Run the command again with `SMITH_VERBOSE=1` and report it to the owner. Do not work around it.
