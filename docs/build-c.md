# smith build

Status: v1, v1.5 (`smith_defer`, `smith_lines`), C-function steps (`.run`, `.args`, `smith_run_lines`,
`smith_run_status`) and the changes found by porting game (below) are implemented. checkers and smith itself build with
it. `.content_hash` is declared but not implemented. The
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
  `smith_glob`, `smith_capture`, `smith_capture_or` and `smith_lines`. These are the calls a later cache would track, and the checks on
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
- A newline stays an error in a path, `description`, `depfile` and a pool name, and in a `-D` argument, because ninja
  cannot hold one there. A command word may hold newlines (a shell script). ninja's `cmd` is one line, so when any word
  of a step's command holds a newline, smith writes the whole command line to `.smith/command/<hash>`: the text it
  would have put in `cmd`, with the stamp's `&& touch` included and single quotes keeping the newlines, and the
  hash is FNV-1a 64 of that text as 16 hex digits. The file ends with a newline, is written through a temporary file
  and a rename only when its bytes differ, and has no `$` doubled, because `sh` reads it and ninja does not. The edge
  becomes `cmd = sh .smith/command/<hash>`. The name changes with the text, so ninja's command comparison reruns the
  step when the script changes. A command with no newline is written as before. Old command files stay (like old
  stamps); `rm -rf .smith` removes them.
- `smith_write` runs when `build.c` runs, not as a step: it writes the file through a temporary file and a rename, only
  when the bytes differ, so a step that lists the file as an input reruns only when the text changed. No step makes the
  file, so `-t clean` leaves it. A path must be relative, with no `..` component and nothing under `.smith/`.
- A step declared again is accepted when every field is equal (command, the three input lists, outputs, depfile,
  description, pool, restat, always) and `*step` becomes the earlier copy. A step that makes an earlier step's output
  with any field different is an error that names the output and the first field that differs. A step without outputs
  is compared by its stamp, which hashes the command and the input lists. The same holds for a task declared twice
  under one name and an alias declared twice (its targets must be equal).
- A task or alias name is a path: `A-Za-z0-9_./-`, not starting with `/` or `-`, not ending with `/`, with no empty,
  `.` or `..` component. The task's output `.smith/task/<name>` then has folders, and is never created. Pool names keep
  `A-Za-z0-9_.-`.
- `smith_option` and `smith_option_value` called again with a declared name and the same kind return what the first
  call returned (the fallback of the first call included), and the description may then be `nullptr`. The other kind is
  an error. A `smith_defer` callback reads the options this way.
- `smith_capture_or` starts the program with its stderr on `/dev/null` and returns the fallback when it cannot start,
  exits non-zero or is killed. Output over 1 MiB is still an error.
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
- `smith_defer` records each call (needs copied, `more` kept) in a table of 256. After `build()` returns, the program
  walks the table in order from 0 up to its current count, which grows when a `more` defers again. An entry whose
  needs all exist (`stat`) has its `more` called once; an entry with a missing need is pending. Nothing builds while
  the program runs, so one pass is enough: an entry that `more` adds is reached later in the same pass. Callbacks run
  from this loop, never from inside each other.
- When the table is not empty, the manifest gains a regeneration rule and edge. Their inputs are every entry's needs,
  pending or not, so a need that changes during the ninja run regenerates the manifest:
  ```
  rule smith_regenerate
    command = .smith/program --regenerate .smith/build.ninja <the -D arguments of this call>
    description = smith: regenerate .smith/build.ninja
    generator = 1
  build .smith/build.ninja: smith_regenerate <every need> | .smith/program
  ```
  `generator = 1` keeps the manifest through `ninja -t clean` and keeps a changed command from forcing a regeneration.
- The program's arguments are `[--regenerate] <manifest> [-D...]`; the driver passes no `--regenerate`, ninja does.
  Without it the manifest is written only when its bytes changed. With it the manifest is always written, even with the
  same bytes, so its mtime moves past the needs and ninja reloads once and settles (ninja decides regeneration by
  mtime only).
- `smith_default` may be missing only while some entry is pending, because the pending `more` will set it. Then the
  manifest has `build smith_nothing: phony` and `default smith_nothing`. With nothing pending, a missing default is
  still the error.
- Measured with ninja 1.13.2. A fresh tree: the run before ninja writes stage 1 (the steps that make the needs, plus the
  regeneration edge); ninja builds the needs, regenerates, reloads, then builds what was asked, so a stage 2 target
  can be named on the command line. Needs present but stale: `more` runs with the old contents, ninja rebuilds the
  needs and regenerates before it builds any target, so stale steps never run. No-op: the needs are older than the
  manifest and ninja prints `no work to do`.
- `smith_lines` reads the file into the arena on every `smith build` (limit 16 MiB). A file a step makes is read inside
  `more`; read in `build()` it is the error `cannot read <path>; if a step makes it, read it inside smith_defer`.

## Two layers

build.c has two layers, and a project may use either or both.

- **The raw layer** (`smith_step`, `smith_task`) is the make replacement: you own every command, and smith knows
  nothing about what a step does. It cannot tell a compile from an `rm`.
- **The high level** (`smith_object`, `smith_program`, `smith_compile_commands`) is for letting smith know what a
  compile and a link are. Each call emits ordinary steps, the same as `smith_step`, and its comment in the header states
  the exact command. `smith build -v` shows it. Nothing is hidden; the gain is that smith can write a correct
  `compile_commands.json`, which clang's `-MJ` cannot (it records Apple clang's internal command, which upstream clangd
  rejects), and which has an entry for build.c itself so an editor finds `<smith/build.h>`.

`compile_commands.json` rules:
- One entry per `smith_object`, in call order: `directory` is the project root, `file` the source, `output` the object,
  and `arguments` the exact argv of the step, `-MMD -MF` included (clangd tolerates them, and the file tells the truth).
  An object declared again with the same output has one entry.
- Then one entry for build.c: `file` "build.c" and `arguments` `smith -std=gnu23 -g -O0 -Wall -Wextra -I .smith/include
  -c build.c`, the flags `smith build` compiles build.c with (a test checks they match smith.c's bootstrap).
- The JSON is escaped (`"`, `\`, control characters as `\u00XX`), indented two spaces, ends with a newline, and is
  written only when its bytes change, through `smith_write`'s rules. A `.run` replay writes nothing.
- Calling `smith_compile_commands` twice is an error, and so is a path under `.smith/`. Call it last.
- A missing required field is an error naming it; so is an object in `.objects` that was never passed to `smith_object`.
  A program's step lists the objects' outputs as inputs and the libraries that are files (a path with `/`, or ending in
  `.a`, `.dylib` or `.so`, and not starting with `-`) as implicit inputs; `-framework X` is not a file.

## The API

Version 1 is enough for checkers and learner. Version 1.5 (`smith_defer`, `smith_lines`) adds what game needs for its
generated mesh list. Everything else is later. The text below is [include/smith/build.h](../include/smith/build.h)
without `#pragma once`. Setting `.content_hash` is the error `smith: build.c: .content_hash is not implemented yet`.

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

typedef struct SmithRun SmithRun;

typedef struct {
    // argv. Only the whole words $in and $out expand; every other $ is literal. A word may hold newlines
    const char *const *command;
    const char *const *inputs;            // $in; a change reruns the step
    const char *const *implicit_inputs;   // a change reruns the step; not on the command line
    const char *const *order_only_inputs; // built first; a change does not rerun the step
    const char *const *outputs;           // $out
    const char *depfile;                  // a file the command writes in -MMD form
    const char *description;              // printed instead of the command
    const char *pool;                     // a name from smith_pool, or "console" for the terminal
    bool restat;                          // an output the command left unchanged does not rerun what depends on it
    bool always;                          // run on every build
    int (*run)(SmithRun *run);            // a C function in build.c instead of .command; 0 succeeds
    const char *const *args;              // given to .run as run->args
    bool content_hash;                    // later: rerun only when the inputs' bytes changed
} SmithStep;

void build(SmithBuild *b); // what the project writes

// one ninja edge. Afterwards every list and string in *step points at the arena copies; with no outputs, .outputs is
// the stamp. A step declared again with every field equal (command, the input lists, outputs, depfile, description,
// pool, restat, always) changes nothing and *step becomes the earlier copy; a step that makes an earlier step's output
// with any field different is an error that names the output and the first field that differs. A command word may
// hold newlines (a shell script): smith writes the whole command line to .smith/command/<hash of its text> and the
// edge runs it with sh, so editing the script reruns the step. A newline in a path, .description, .depfile or .pool is
// an error.
void smith_step(SmithBuild *b, SmithStep *step);
// `smith build <name>`: runs every time it is asked for, like a make .PHONY target
// afterwards .outputs is {name}, so other steps can depend on the task. A name holds A-Z a-z 0-9 _ . / -, may not
// start with '/' or '-', end with '/', or hold an empty, '.' or '..' component (check-alone/src/a.c is a name). A task
// declared again under one name with every field equal changes nothing; with any field different it is an error
void smith_task(SmithBuild *b, const char *name, SmithStep *step);
// a group of targets, named by the same rules as a task; declared again with equal targets it changes nothing, with
// different targets it is an error
void smith_alias(SmithBuild *b, const char *name, const char *const *targets);
void smith_default(SmithBuild *b, const char *const *targets);
void smith_pool(SmithBuild *b, const char *name, int depth);

// -Dname. An option can be read again: the same name and kind returns the same value and the description may then be
// nullptr, so a smith_defer callback can read what build() read. The other kind is an error
bool smith_option(SmithBuild *b, const char *name, const char *description);
// -Dname=value; fallback may be nullptr
const char *smith_option_value(SmithBuild *b, const char *name, const char *fallback, const char *description);
// an environment variable, or fallback (may be nullptr)
const char *smith_env(SmithBuild *b, const char *name, const char *fallback);
// the union of the patterns' matches, sorted by strcmp, each name once: make's $(sort $(wildcard a b c)). No **
const char *const *smith_glob(SmithBuild *b, const char *const *patterns);
// a command's stdout without its last newline; no shell. A program that cannot start, exits non-zero or is killed is an
// error
const char *smith_capture(SmithBuild *b, const char *const *argv);
// like smith_capture, but returns fallback (may be nullptr) when the program cannot start, exits non-zero or is
// killed; its stderr goes to /dev/null
const char *smith_capture_or(SmithBuild *b, const char *const *argv, const char *fallback);
// Writes a file when build.c runs, through a temporary file and a rename, and only when its bytes differ, so its mtime
// moves only when its content does. It creates missing parent folders. The path must be relative, with no '..'
// component and nothing under .smith/. For files whose content build.c computes; a step that reads one lists it as an
// input. `-t clean` leaves it, since no step makes it
void smith_write(SmithBuild *b, const char *path, const char *text);

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

// Steps that exist only once a generated file is read.
// `needs` are paths: outputs of steps already declared, or source files. Once they all exist, `more` is called, and it
// may call any function here, smith_defer included. The program calls `more` itself while it runs, for each call whose
// needs exist, in order (limit 256 calls). A call whose needs do not exist yet is pending: the manifest gets a
// regeneration edge on every call's needs, so ninja builds the needs, runs the program again with them in place, and
// reloads. A pending `more` may be the one that calls smith_default.
void smith_defer(SmithBuild *b, const char *const *needs, void (*more)(SmithBuild *b));
// One entry per line of the file, without the newline; a final newline adds no entry and an empty line is "". The file
// is read on every `smith build`. A file that a step makes is read inside smith_defer's `more`, never in build(). A
// missing file or one over 16 MiB is an error.
const char *const *smith_lines(SmithBuild *b, const char *path);

// Steps written as C functions. A step or task with .run (and no .command) calls it when ninja runs the step: the
// build program runs again with `--run <the step's first output>`, repeats build() and the smith_defer callbacks
// without writing anything (smith_write included, until the function runs), and calls the function. Its return value is
// the exit status. Like a command, it writes the outputs it declares; one with none gets smith's stamp. It sees its
// step's own lists, and run->b works with every helper (smith_fmt, smith_cat, smith_lines, smith_write). Editing
// build.c reruns every .run step, since the function's code is part of the program. smith_capture and smith_glob run
// again in the replay. .content_hash is not implemented yet and exits 2.
struct SmithRun {
    SmithBuild *b;
    const char *const *inputs;  // the step's .inputs
    const char *const *outputs; // its outputs, the stamp included; empty for a task
    const char *const *args;    // its .args
};
// Runs a program, no shell, and returns its stdout split into lines as smith_lines does, with its stderr passed
// through. A program that cannot start, exits non-zero or is killed prints `smith: <argv[0]> failed (status N)` and
// gives nullptr. More than 1 MiB of stdout is an error.
const char *const *smith_run_lines(SmithRun *run, const char *const *argv);
// Like smith_run_lines, but the exit status is the answer: the program's exit status, 128 plus the signal that killed
// it, or 127 when it cannot start. *lines (may be nullptr) receives its stdout lines, or nullptr when it did not start.
// It prints no message.
int smith_run_status(SmithRun *run, const char *const *argv, const char *const **lines);

// High level: compiles and links that smith knows are compiles and links, so it can write compile_commands.json.
// Each emits ordinary steps, the same as calling smith_step yourself; the command is written out in its comment.

// One source compiled to one object:
//   <compiler> <flags...> -MMD -MF <output with .d for its extension> -c <source> -o <output>
// with that .d file as the step's depfile.
typedef struct {
    const char *compiler;               // the program, such as "smith" or "smith++"; required
    const char *source;                 // required
    const char *output;                 // the object, such as "build/main.o"; required
    const char *const *flags;           // exactly these, in this order
    const char *const *implicit_inputs; // files the compile reads that are not in its depfile on a clean build, such as
                                        // a generated #include or #embed file
    SmithStep step;                     // set by smith_object: the step it emitted
} SmithObject;

// Objects linked into a program:
//   <linker> <flags...> <each object's output...> <libraries...> -o <output>
typedef struct {
    const char *linker;                // the program, such as "smith" or "smith++"; required
    const char *output;                // the program file; required
    const char *const *flags;          // before the objects, such as -mmacosx-version-min=26.0
    const SmithObject *const *objects; // the objects to link, each already passed to smith_object
    const char *const *libraries;      // after the objects: -framework Metal, -lm, a .a path
    SmithStep step;                    // set by smith_program: the step it emitted
} SmithProgram;

void smith_object(SmithBuild *b, SmithObject *object);
void smith_program(SmithBuild *b, SmithProgram *program);
// Writes compile_commands.json at path when build.c runs, only when its bytes change, from every smith_object so far,
// plus one entry for build.c itself with the command smith compiles it with. Call it last.
void smith_compile_commands(SmithBuild *b, const char *path);
```

Rules:
- A list ends in `nullptr`, and a list is never itself `nullptr`. An empty list is `(const char *[]){nullptr}`;
  `smith_glob` and `smith_when` return one when there is nothing. A loop starts from one:
  `objects = smith_cat(b, objects, cc.outputs, nullptr)`. The `nullptr` that ends `smith_cat`'s arguments is the only
  one.
- A compound literal lives only until the end of its block. Every smith call copies what it is given, so a literal
  passed straight to a call is safe, and what a call returns lives until the build ends. A literal returned from a
  function, or stored in a struct that outlives its block, dangles: the game port's raw translation crashed on one, and
  ASan did not catch it.
- `smith_step` and `smith_task` take the step by pointer and update it in place. They copy every list and string into
  the arena, so a block-scope compound literal is safe, and afterwards the step's lists point at the copies:
  `.inputs = compile.outputs` is valid after the block ends. Declare the step first, then pass its address:
  clang-format indents a call that holds the whole `(SmithStep){...}` very deeply, and this form formats cleanly.
- A step with no outputs gets a stamp list in `.outputs`, and a task gets `{name}`.
- A task has no outputs, `.restat` or `.always`; setting one is an error.
- `smith_default` is required.
- `.command` and `.run` are exclusive: setting both is an error, and so is `.args` without `.run`.

### C functions as steps (`.run`)

A recipe that is a program, such as game's `check-symbols` or `check-libc`, is a C function in build.c. It reads build
outputs (objects, programs), so it runs during the build, after they exist; ninja calls it as a step.

- **The generator rule.** The edge's command is
  `.smith/program --run <the step's first output> <the -D arguments of this call, quoted>`, followed by the stamp's
  `&& touch` for a step with no outputs. A task's first output is `.smith/task/<name>`. Because the `-D` arguments are
  in the command text, a different option reruns the step. Every `.run` edge also gets the implicit input
  `.smith/program`: the function's code is part of the program, so editing build.c reruns every `.run` step. That is
  conservative and correct, not minimal.
- **The replay.** `--run` runs `build()` and the `smith_defer` callbacks exactly as when writing the manifest, with the
  same `-D` arguments, so the same steps exist (function pointers do not survive between processes, so there is no
  table of them). It writes neither the manifest, nor `.smith/command` files, nor the `smith_write` files of
  `build()`; `smith_write` writes only when called from the function. It finds the step whose first output is the
  argument and calls its function; the return value is the process's exit status, so ninja fails the step and nothing
  after it runs. `smith_capture` and `smith_glob` run again in every replay. Measured: a `.run` task that does nothing
  costs 1 to 3 ms more than a `true` task (about 10 ms against 9 ms per `smith build` in the test), so keep a slow
  `smith_capture` in the step's command rather than in `build()` when many `.run` steps exist.
- **The function.** `int f(SmithRun *run)` returns 0 for success. `run->inputs`, `run->outputs` (the stamp included;
  empty for a task) and `run->args` are the step's own lists, and `run->b` works with every helper. Like a command, it
  writes the outputs it declares; a step with none gets smith's stamp, touched after a zero return. With `.restat`, a function that
  writes its output with `smith_write` gives early cutoff: unchanged bytes keep the mtime and nothing after it reruns.
- **Two functions with the same inputs and args and no outputs** get the same stamp name, which is an error that names
  `.run`; give one of them an output or different `.args`.
- `smith_run_lines(run, argv)` runs a program with no shell and returns its stdout as lines (stderr passes through); a
  program that cannot start, exits non-zero or is killed prints `smith: <argv[0]> failed (status N)` and gives `nullptr`.
  `smith_run_status(run, argv, &lines)` returns the status instead: the exit status, 128 plus the signal, or 127 when
  the program cannot start, and prints nothing. Both limit stdout to 1 MiB.
- `.pool = "console"` gives the step the terminal. ninja allows one console step at a time.
- The calls that read the outside world are read again on every `smith build`, because the program reruns each time.
- `smith_glob` takes a list of patterns, so one pattern is a list of one: `smith_glob(b, (const char *[]){"src/*.c",
  nullptr})`. The result is the union, sorted by `strcmp` with each name once, like make's `$(sort $(wildcard a b c))`.

Which tier each project needs:
- checkers: v1.
- smith: v1. Its `build.c` is at the repository root and replaced its Makefile.
- learner: v1.
- game: v1.5, for the mesh list that a program prints and that the rest of the build depends on (`smith_defer`,
  `smith_lines`). Implemented.

Dropped from the proposals of the case agents:
- `smith_list`: a compound literal does it, and a function that takes a variadic list needs a terminator anyway.
- `smith_in`: membership is a loop. The `function` case writes `in_list` in six lines.
- `smith_read`: `smith_lines` covers the cases. A whole-text read can come back if a case needs it.
- `smith_run_temp` and `smith_run_status`: no case needs them yet.

## Found by porting game

game's Makefile was ported to a `build.c` of about 1,000 lines. The port worked around these gaps, and each is now in
the API:
- **Multi-line command words.** The Makefile's recipes are shell scripts, and the port joined their lines with a
  heuristic. A word may now hold newlines; smith moves such a command into `.smith/command/<hash>` for ninja.
- **`smith_write`.** The unity files and the test list are computed by `build.c`, and the Makefile wrote them with a
  temporary file and `cmp`. The mtime now moves only when the content does, with no `.always` step that prints on every
  no-op build.
- **`smith_capture_or`.** `$(shell git ... 2>/dev/null)` yields a fallback outside a repository, but `smith_capture`
  is fatal when the command fails.
- **An identical step declared twice is accepted.** game's bakers share object files such as `sim/noise`, and the port
  kept a list of what it had declared. The same holds for a task or an alias.
- **`/` in task and alias names.** The check-alone tasks are named after files, such as `check-alone/src/base/arena.c`.
- **Options can be read again.** A `smith_defer` callback has no context, so it could not read the options that
  `build()` read.
- **`smith_glob` takes a list of patterns.** `$(sort $(wildcard a b c))` needed a sorting, de-duplicating helper.

Not changed: lazy values such as `$(xcrun --show-sdk-path)` (the port wraps the command in `sh -c`), quiet `.always`
steps and `.content_hash`.

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
| function | a step written as a C function | workaround | workaround | workaround | native | `.run`, `.args`, `smith_run_lines`, `smith_run_status` (v2) |
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
