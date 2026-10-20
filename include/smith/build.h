// smith/build.h: the API of a project's build.c, which `smith build` compiles and runs to write build.ninja.
// Design: docs/build-c.md. Lists end in nullptr and are never nullptr (an optional field left unset reads as empty).
#pragma once

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
