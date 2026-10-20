# smith build

Status: v1 is implemented. checkers and smith itself build with it. v1.5 and later are declared but not implemented. The
23 cases that shaped it, each in make, ninja, Zig 0.17.0 and a smith `build.c`, are in
[build-c-cases.md](build-c-cases.md).

## What smith build is

- `build.c` describes the build. It is a C program, so loops, functions and `if` are the language, not a macro system.
- `smith build` compiles `build.c` with one fixed command of its own, runs it, and the program writes `build.ninja`.
- smith then runs ninja on that file. ninja does the scheduling, the rebuild decisions and the parallelism.
- It replaces the Makefile. It is step 2 of the three steps in [CLAUDE.md](../CLAUDE.md): it only describes the build, so
  it changes nothing the build does.

## Principles

- **Everything is explicit and there are no defaults.** Commands are written out in full. Replacing a Makefile with a
  `build.c` changes nothing. In particular, object paths stay the same as the Makefile's, because the linked binary
  records them.
- **No macros.** A list is a C23 compound literal that ends in `nullptr`. A loop or a function builds the rest.
- **C23, libc only, no malloc.** Strings and lists live in smith's arena until `build` returns.
- **build.c reads the outside world through the `smith_*` calls**: `smith_option`, `smith_option_value`, `smith_env`,
  `smith_glob`, `smith_capture` and `smith_lines`. These are the calls a later cache would track, and the checks on
  `build.c` (a lint rule, the option listing) know about them. In v1 `getenv` and `fopen` would also work, because the
  program reruns on every `smith build`. Use the calls anyway, so the build keeps working when a cache arrives.
- **The build program runs on every `smith build`.** Globs, environment variables, captured commands and options are
  always current, and nothing is recorded or compared.
- Zig 0.17.0 shows the failure of the alternative. It caches the result of running `build.zig`, and it silently missed a
  new file, a changed list and a changed environment variable whenever the read was not declared (measured: `wildcard`,
  `monad1`, `system2`). That is why v1 reruns instead of caching.

## How smith build runs

The driver, in order:
1. `build.c` must be in the current folder.
2. Find ninja: `$SMITH_NINJA`, else the first `ninja` on PATH.
3. Split the arguments. Those that start with `-D` go to the build program, and everything else goes to ninja. `-f` and
   `-C` are refused, because `smith build` names the manifest itself.
4. Write `.smith/include/smith/build.h`, `.smith/runtime.c`, `.smith/bootstrap/build.ninja` and `.smith/.gitignore`.
   Each is written only when its bytes changed, so its mtime changes only then.
5. Run `ninja --quiet -f .smith/bootstrap/build.ninja`. It compiles `build.c` into `.smith/program`, and
   rebuilds the program when `build.c`, a header it includes, or smith's runtime changed (a depfile).
6. Run the program with the `-D` arguments. `build()` calls the `smith_*` functions, and the program writes
   `.smith/build.ninja` only when its bytes changed.
7. `execv` ninja on `.smith/build.ninja` with the other arguments, so ninja's exit status and signals are
   `smith build`'s.

The header and the runtime are embedded in smith with C23 `#embed`, so `smith build` needs nothing installed beside it.
The compile command is `<clang> -std=gnu23 -g -O0 -Wall -Wextra -I .smith/include`, with the clang that `smith`
itself would pick. Bootstrapping still works with one `cc` command.

smith's own files all go in `.smith/`: the manifests, ninja's logs (`builddir`), the build program, stamps and task
outputs. The bootstrap manifest has its own `builddir`, `.smith/bootstrap`, because ninja recompacts its log and
drops the entries that the current manifest does not have.

They are not in `build/`, because a project's `clean` task usually runs `rm -rf build`, and that would delete the logs
while ninja is still running from them. Ninja then exits 1 with "Error writing to build log" (measured). smith writes
`.smith/.gitignore` containing `*`, so git ignores the folder with no change to the project, as pytest, ruff and mypy
do for their caches. smith does not create `build/`.

An unchanged manifest keeps its mtime, so a `smith build` with nothing to do does not make ninja rebuild.

Built in, as ninja's own tools passed through:
- `smith build -t clean` removes what ninja built.
- `smith build -t compdb` writes `compile_commands.json`.
- `smith build -t targets` lists the targets.
- `-j`, `-n` and `-v` work as in ninja.
- A project's own `clean` task is just a task.

Generator rules:
- Only the whole words `$in` and `$out` expand. smith expands them itself. ninja evaluates an edge variable such as
  `cmd` when it reads the line, before the edge's `$in` and `$out` exist, so `$in` there would be empty. smith writes
  the step's input and output paths into `cmd`, each shell-quoted like a literal word. Every `$` in `cmd` is doubled.
- Literal words in a command are shell-quoted. Paths in `build` lines are escaped (`$ `, `$:`, `$$`).
- `deps = gcc` is emitted whenever `.depfile` is set, so ninja folds the `.d` file into `.ninja_deps` and deletes it.
- `.always` adds an input on one shared phony target that has no inputs (`build smith_always: phony`).
- A step with no outputs gets a stamp output, `.smith/stamp/<hash>`, and `&& touch <stamp>` ends its command. The
  stamp is touched only when the command succeeds, so the step reruns only when its inputs change (case `nofileout`).
  The hash is FNV-1a 64 of the command and every input, so two steps with one command and different inputs get two
  stamps.
- A task runs every time it is asked for, like make's `.PHONY`. It is an edge whose output is
  `.smith/task/<name>`, which is never created, plus `build <name>: phony .smith/task/<name>`. A task cannot
  have outputs, `.restat` or `.always`; setting one is an error.
- `smith_default` is required. Without it ninja would build every target, tasks such as `clean` included, so a
  `build.c` without a default is an error. An empty default, such as a glob that matched nothing, builds nothing: smith
  writes `build smith_nothing: phony` and `default smith_nothing`.
- A `-D` value lasts for one `smith build` call, as a make variable on the command line does. The build program gets it
  as an argument and writes the manifest for it (case `options`). A `-D` that `build.c` never declared is an error that
  lists the declared options.
- There is no automatic `cleandead`. make never deletes leftover outputs, so smith does not either.
  `smith build -t cleandead` is there when wanted (case `noleftover`).
- `smith_defer` writes a first manifest that has a regeneration edge (`generator = 1`) from the build program. ninja
  builds the needs, runs that edge, and the build program calls `more`. The edge stays in every later manifest.

## The API

Version 1 is enough for checkers and learner. Version 1.5 adds what game needs for its generated mesh list. Everything
else is later. The text below is [include/smith/build.h](../include/smith/build.h) without `#pragma once`.
The v1.5 and later functions are declared but not implemented yet. Calling one, or setting `.run`, `.args` or
`.content_hash`, is the error `smith: build.c: <name> is not implemented yet`.

```c
// smith/build.h: the API of a project's build.c, which `smith build` compiles and runs to write build.ninja.
// Design: docs/build-c.md. Lists end in nullptr and are never nullptr (an optional field left unset reads as empty).

typedef struct SmithBuild {
    // `uname -s` exactly: "Darwin", "Linux"; nothing renamed, so it matches what a Makefile sees
    const char *os;
    const char *arch; // `uname -m` exactly: "arm64", "x86_64", "aarch64"
    const char *root; // the project's absolute folder, where build.c is
    int cpus;         // the number of cores
} SmithBuild;

typedef struct SmithRun SmithRun; // later

typedef struct {
    const char *const *command;           // argv. Only the whole words $in and $out expand; every other $ is literal
    const char *const *inputs;            // $in; a change reruns the step
    const char *const *implicit_inputs;   // a change reruns the step; not on the command line
    const char *const *order_only_inputs; // built first; a change does not rerun the step
    const char *const *outputs;           // $out
    const char *depfile;                  // a file the command writes in -MMD form
    const char *description;              // printed instead of the command
    const char *pool;                     // a name from smith_pool, or "console" for the terminal
    bool restat;                          // an output the command left unchanged does not rerun what depends on it
    bool always;                          // run on every build
    int (*run)(SmithRun *run);            // later: a C function in build.c instead of .command
    const char *const *args;              // later: given to .run
    bool content_hash;                    // later: rerun only when the inputs' bytes changed
} SmithStep;

void build(SmithBuild *b); // what the project writes

// one ninja edge. Afterwards every list and string in *step points at the arena copies; with no outputs, .outputs is
// the stamp
void smith_step(SmithBuild *b, SmithStep *step);
// `smith build <name>`: runs every time it is asked for, like a make .PHONY target
// afterwards .outputs is {name}, so other steps can depend on the task
void smith_task(SmithBuild *b, const char *name, SmithStep *step);
void smith_alias(SmithBuild *b, const char *name, const char *const *targets);
void smith_default(SmithBuild *b, const char *const *targets);
void smith_pool(SmithBuild *b, const char *name, int depth);

// -Dname
bool smith_option(SmithBuild *b, const char *name, const char *description);
// -Dname=value; fallback may be nullptr
const char *smith_option_value(SmithBuild *b, const char *name, const char *fallback, const char *description);
// an environment variable, or fallback (may be nullptr)
const char *smith_env(SmithBuild *b, const char *name, const char *fallback);
const char *const *smith_glob(SmithBuild *b, const char *pattern); // sorted
// a command's stdout without its last newline; no shell
const char *smith_capture(SmithBuild *b, const char *const *argv);

// Helpers
[[gnu::format(printf, 2, 3)]] const char *smith_fmt(SmithBuild *b, const char *format, ...);
// joins lists; the last argument is nullptr
const char *const *smith_cat(SmithBuild *b, const char *const *first, ...);
// format applied to each item ("out/%s")
const char *const *smith_map(SmithBuild *b, const char *const *items, const char *format);
// items, or an empty list
const char *const *smith_when(SmithBuild *b, bool condition, const char *const *items);
// ("src/a.c", ".o") gives "src/a.o"
const char *smith_replace_extension(SmithBuild *b, const char *path, const char *extension);

// Not implemented yet: calling one prints a message and exits 2.
// builds needs, regenerates, calls more
void smith_defer(SmithBuild *b, const char *const *needs, void (*more)(SmithBuild *b));
// tracked; one entry per line
const char *const *smith_lines(SmithBuild *b, const char *path);

// later: steps written as C functions
struct SmithRun {
    SmithBuild *b;
    const char *const *inputs;
    const char *const *outputs;
    const char *const *args;
};
// runs a program, no shell; stdout as lines, or nullptr after a message
const char *const *smith_run_lines(SmithRun *run, const char *const *argv);
```

Rules:
- A list ends in `nullptr`, and a list is never itself `nullptr`. An empty list is `(const char *[]){nullptr}`;
  `smith_glob` and `smith_when` return one when there is nothing. A loop starts from one:
  `objects = smith_cat(b, objects, cc.outputs, nullptr)`. The `nullptr` that ends `smith_cat`'s arguments is the only
  one.
- `smith_step` and `smith_task` take the step by pointer and update it in place. They copy every list and string into
  the arena, so a block-scope compound literal is safe, and afterwards the step's lists point at the copies:
  `.inputs = compile.outputs` is valid after the block ends. Declare the step first, then pass its address:
  clang-format indents a call that holds the whole `(SmithStep){...}` very deeply, and this form formats cleanly.
- A step with no outputs gets a stamp list in `.outputs`, and a task gets `{name}`.
- A task has no outputs, `.restat` or `.always`; setting one is an error.
- `smith_default` is required.
- `.command` and `.run` are exclusive. A `.run` step is later; nothing in v1 or v1.5 needs it.
- `.pool = "console"` gives the step the terminal. ninja allows one console step at a time.
- The calls that read the outside world are read again on every `smith build`, because the program reruns each time.

Which tier each project needs:
- checkers: v1.
- smith: v1. Its `build.c` is at the repository root and replaced its Makefile.
- learner: v1.
- game: v1.5, for the mesh list that a program prints and that the rest of the build depends on (`smith_defer`,
  `smith_lines`).

Dropped from the proposals of the case agents:
- `smith_list`: a compound literal does it, and a function that takes a variadic list needs a terminator anyway.
- `smith_in`: membership is a loop. The `function` case writes `in_list` in six lines.
- `smith_read`: `smith_lines` covers the cases. A whole-text read can come back if a case needs it.
- `smith_run_temp` and `smith_run_status`: no case needs them yet.

## Example: checkers

checkers builds with this `build.c`, which replaced its Makefile command for command:
```c
// The build of checkers, run with `smith build`. The commands are the ones the Makefile ran.
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep checkers = {
        .command = (const char *[]){"smith", "-std=c23", "-MJ", "checkers.json", "-g", "-O0", "checkers.c", "-o",
                                    "checkers", nullptr},
        .inputs = (const char *[]){"checkers.c", nullptr},
        .outputs = (const char *[]){"checkers", "checkers.json", nullptr},
    };
    smith_step(b, &checkers);

    // clangd reads compile_commands.json: the -MJ fragment inside brackets, without its trailing comma.
    SmithStep compile_commands = {
        .command = (const char *[]){"sh", "-c",
                                    "echo '[' > compile_commands.json && "
                                    "sed '$ s/,$//' checkers.json >> compile_commands.json && "
                                    "echo ']' >> compile_commands.json",
                                    nullptr},
        .inputs = (const char *[]){"checkers.json", nullptr},
        .outputs = (const char *[]){"compile_commands.json", nullptr},
    };
    smith_step(b, &compile_commands);

    SmithStep run = {
        .command = (const char *[]){"./checkers", nullptr},
        .inputs = (const char *[]){"checkers", nullptr},
        .pool = "console",
    };
    smith_task(b, "run", &run);

    SmithStep clean = {
        .command = (const char *[]){"rm", "-f", "checkers", "checkers.json", "compile_commands.json", nullptr},
    };
    smith_task(b, "clean", &clean);

    smith_default(b, smith_cat(b, checkers.outputs, compile_commands.outputs, nullptr));
}
```

- `smith build` builds `checkers` and `compile_commands.json`; `smith build run` builds what is needed, then runs the
  game on the terminal every time; `smith build clean` removes what the Makefile's `clean` removed. `smith build -t
  clean`, ninja's tool, removes the same three files, because each is a declared output.
- Compared in one scratch folder, make against `smith build`: the machine code is identical, and `checkers.json` and
  `compile_commands.json` are identical once the temporary object's name is set aside. clang compiles and links in one
  step through a randomly named temporary object, so the binary's UUID, its debug map (that object's name and mtime) and
  its signature differ between two make builds as well, in the same places.

## The 23 cases

Cells come from each case's "How each handles it" line. "native" is built in, "workaround" is possible with extra
machinery written by the user, "partly" is some of the behaviour, "no" is not possible on its own. The smith column is
what a `build.c` author writes, so `system1` is a workaround in ninja but native in smith, through `.always`.

| case | what it tests | make | ninja | Zig 0.17.0 | smith | smith API |
|---|---|---|---|---|---|---|
| basic | copy a file; rerun if the input changed or the output is missing | native | native | native | native | nothing new |
| parallel | two independent outputs at the same time | native (needs `-j`) | native | native | native | nothing new |
| include | rebuild when a transitively included header changes | workaround | native | native | native | `.depfile` (v1) |
| wildcard | build `X.out` from `X.in` for any `X` | native | no | workaround | native | `smith_glob`, `smith_replace_extension` (v1) |
| spaces | file names with spaces | workaround | native | native | native | nothing new |
| monad1 | depend on the files named in another file | native | no | workaround | native | `smith_lines` (v1.5) |
| monad2 | a generated list of inputs that already exist | native | native | native | native | nothing new |
| monad3 | a generated list names files built by steps that exist only if the list says so | native | workaround | workaround | native | `smith_defer`, `smith_lines` (v1.5), `smith_map` (v1) |
| unchanged | a generator reruns but writes the same bytes | partly | native | partly | native | `.restat` (v1) |
| multiple | one step writes two files, only one changes | workaround | native | no | native | `.restat` (v1) |
| system1 | a step that runs every build; dependents rerun only if its result changed | native | workaround | native | native | `.always`, `.restat` (v1) |
| system2 | rerun when the output is missing or an environment variable changed | workaround | workaround | workaround | native | `smith_env` (v1) |
| pool | limit one stage to 2 concurrent runs | workaround | native | workaround | native | `smith_pool`, `.pool` (v1) |
| digest | do not rerun when the input is rewritten with the same bytes | workaround | workaround | workaround | later | `.content_hash` (later) |
| nofileout | rerun if and only if the input changed, with no output file | workaround | workaround | native | native | `smith_step` (v1) |
| noleftover | delete outputs that are no longer produced | no | workaround | workaround | workaround | nothing new; the user runs `smith build -t cleandead` |
| secondary | keep an auxiliary file out of the rebuild decision | native | workaround | partly | workaround | `.implicit_inputs`, `.order_only_inputs` (v1) |
| intermediate | an auxiliary file deleted at the end, not recreated | native | workaround | native | workaround | nothing new |
| variants | one program built twice (plain and sanitized) | workaround | native | native | native | `smith_cat`, `smith_fmt` (v1) |
| tool | build a program, run it, `#embed` its output | native | native | workaround | native | nothing new |
| options | a task with values from the command line | native | no | native | native | `smith_option_value`, `smith_task`, `smith_when` (v1) |
| function | a step written as a C function | workaround | workaround | workaround | native | `.run`, `.args`, `smith_run_lines` (later) |
| platform | a source file and a flag that differ by platform | native | no | native | native | nothing new |

- make and ninja: measured, except make in `digest` and `noleftover` (not run; they have the same shape as the standard
  workarounds) and Linux in `platform`.
- Zig: run on 0.17.0.
- smith: checked by running the hand-written `build.ninja` that the `build.c` would generate. `build.c` itself
  has not run in these cases; the v1 implementation has its own tests. In `function` the ninja file ran against a stand-in for the build program, and the replay step is not verified.
  In `monad3` the two manifests were written by hand, and the regeneration was done by a script standing in for the
  build program.
- Every smith snippet is a complete `build.c` that passes `smith -std=gnu23 -fsyntax-only -Wall -Wextra -Werror` against
  `smith/build.h`. That checks the API compiles, not that it works.
- Full cases: [build-c-cases.md](build-c-cases.md).

## What we measured

Measured on macOS with Apple clang 21, GNU Make 3.81 and ninja 1.13.2.
- A no-op `make` in checkers, learner and game takes 0.05 to 0.11 s.
- game's clean `make check` takes 18.5 s, of which the static analyzer is 14.5 s.
- So ninja alone would not make these projects faster. The real wins would be early cutoff (do not rerun what depends on
  an output that did not change) and a content cache (do not rerun a step whose inputs have bytes seen before).

The Zig 0.17.0 findings that shaped the design, from the cases:
- Configure caching. `build.zig` is not rerun on every `zig build`. A read of a directory, a file or the environment is
  missed unless declared. smith v1 reruns `build.c` on every `smith build` instead, so no read can be missed.
- Arguments are compared by size, inode and mtime. A same-size in-place edit that keeps the mtime is missed, and a
  same-bytes rewrite reruns the step. Files named in a depfile are compared by content. This is why `digest` works only
  through a depfile in Zig.
- `multiple` fails. Both outputs of one step share a cache folder named by a hash of the step's inputs, so any input
  change reruns both consumers. ninja with `restat` and make with a pattern rule rerun only one.
- There are no custom steps (`function`). A check written in the build language has to run as a shell script or a
  program.
- A deleted cache file is not recreated (`secondary`, `system2`). The build fails until the inputs change.

## Pitfalls found

- **Manifest regeneration is decided by mtime only.** If smith rewrites a stage 1 manifest over an up-to-date tree, the
  new manifest is newer than `list`, ninja sees nothing to do, and `output` never appears. Verified, even with a
  different command string. The fix, verified: set the stage 1 manifest's mtime to 1970 after writing it, or call `more`
  only when its needs already exist.
- **`ninja -n` on a fresh tree shows only stage 1.** A dry run cannot reload the manifest. ninja's `-t` tools and `-n`
  see whichever manifest is on disk.
- **A generated `#embed` or `#include` file must be an implicit input**, not left to the depfile. LLVM 23 clang with
  `-MMD -MF` and a missing `#embed` file exits 0, writes a 512-byte object and no `.d` file. Without `-MMD` it is a
  fatal error, and Apple clang 21 fails correctly. With only the depfile, a clean build "succeeded" with 2 edges and
  bake never ran.
- **`smith build -t cleandead` only knows files in ninja's log.** If `.ninja_log` is deleted, old outputs stay. Its
  message count did not match its dry run once, so check the files, not the count. A file that smith used to build and
  the project now keeps as a hand-written source at the same path would be deleted. smith never runs it by itself.
- The generator edge needs `generator = 1`, so that `ninja -t clean` keeps the manifest.
- A task named `test` next to a folder named `test` still ran in ninja 1.13.2, but this is not established to be safe.
  So a task is an edge with output `.smith/task/test` plus `build test: phony .smith/task/test`.
- A value read with `smith_env` ends up in `build.ninja` and `.ninja_log`. That matters if it is a secret.

## Bootstrapping

- smith itself stays plain C23 with libc only, so a fresh clone builds with one command and any C23 compiler:
  `cc -std=gnu23 smith.c -o smith` (checked with Apple clang 21). That smith then runs smith's own `build.c`, which is
  the second project built by `build.c`, after checkers: `smith build`, `smith build test`, `smith build check`,
  `smith build install` and the rest replaced the Makefile's targets, with the same compile command and the same
  machine code.
- The same command is the way back when an installed smith is broken and `smith build` cannot run, and the first build
  of a fresh clone: `mkdir -p build && cc -std=gnu23 smith.c -o build/stage0 && build/stage0 build install -DCC=cc`. It
  needs only a C23 compiler.
- Other projects need smith installed to run their `build.c`, as a Makefile needs make. So does smith: the installed
  smith must have `smith build`.

## Open decisions

- **`b->os` is the host.** Cross-compiling needs a target too, next to `os` (case `platform`). Zig has a separate target
  and host; make's `uname` has the same fault.
- **`smith_capture` reruns its command on every `smith build`**, as the whole program does. It costs about 0.04 s for
  `brew --prefix llvm`. make ran it only when a target needed it. Options: a cache by command, or let `build.c` skip the
  call unless a target needs it.
- **A cache of the build program's result.** v1 reruns it every time. A cache would track the `smith_*` reads and rerun
  `build.c` only when one changed.
- **Whether to enforce reads through the `smith_*` calls with a lint rule on `build.c`.** It would flag `getenv`,
  `fopen` and `popen`.
- **`content_hash` versus `restat`.** `restat` cuts off what comes after a step that already ran. `content_hash` would
  stop the step itself, with hash edges generated by smith (case `digest`). Neither covers files found through a
  depfile.
- **An executor of our own, later.** Study samurai, a C reimplementation of ninja, without vendoring it: it uses malloc.
  Differential tests against ninja would show where the two differ.
