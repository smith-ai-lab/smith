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
// the union of the patterns' matches, sorted by strcmp, each name once: make's $(sort $(wildcard a b c)).
// A `**` component matches zero or more folders, so src/**/*.c matches src/a.c and src/x/y/b.c (symbolic links are
// not followed, and more than 64 folders deep is an error). Every other component goes through fnmatch, the last one
// included, and a hidden entry matches only a component that starts with '.', as in the shell
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
// the items with the separator between them, in the arena; the empty list gives "". For text such as a unity file:
// smith_join(b, smith_map(b, files, "#include \"%s\"\n"), "")
const char *smith_join(SmithBuild *b, const char *const *items, const char *separator);
// the strings up to a final nullptr, as a list in the arena. Unlike a compound literal it never dangles, so it is safe
// to keep and to return from a function; smith_list(b, nullptr) is the empty list
const char *const *smith_list(SmithBuild *b, const char *first, ...);
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

// Steps written as C functions. A step or task with .run (and no .command) calls it when ninja runs the step. The
// step's command carries everything the function gets: `.smith/program --run <function> --input <path>... --output
// <path>... --arg <word>...`, where <function> is the function's address minus the address of a fixed function in the
// program, in hex (the program is each .run edge's implicit input, so the offset is valid for the run that uses it).
// With --run the program does not call build() or any smith_defer callback: it makes a SmithBuild (os, arch, root,
// cpus, a fresh arena) and a SmithRun from those words, calls the function and exits with its result. So a function
// sees no state build() set, no -D option and no earlier capture: its data is its step's .inputs, .outputs and
// .args, and nothing else. Pass an option's value in .args. The offset is checked to land in this executable.
// The return value is the exit status. Like a command, the function writes the outputs it declares; one with none gets
// smith's stamp. run->b works with every helper that needs no build() state (smith_fmt, smith_cat, smith_join,
// smith_list, smith_lines, smith_glob, smith_capture, smith_write). Editing build.c reruns every .run step, since the
// function's code is part of the program. .content_hash is not implemented yet and exits 2.
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
// Runs a program with no shell and with its stdout and stderr going straight to the terminal (inherited, not
// captured). `environment` (NAME=value entries, may be nullptr) is added to the current environment, and an entry
// replaces a variable of the same name. Returns the exit status, 128 plus the signal that killed it, or 127 when it
// cannot start. stdout and stderr are flushed first, so the function's own printing comes before the program's. The
// same flush happens before smith_run_lines and smith_run_status spawn.
int smith_run_program(SmithRun *run, const char *const *argv, const char *const *environment);

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
// Records a compile_commands.json entry for source with no step: {directory: the root, file: source, arguments:
// [compiler, flags..., "-c", source]} and no "output". It is for files the build never compiles on their own, such as
// sources reached only through a unity build, so the editor still knows their flags. compiler and source are required.
// Entries follow the smith_object entries, in call order, before build.c's. A source that already has a smith_object
// entry or an earlier smith_compile_command is skipped: the first entry for a file wins. Call it before
// smith_compile_commands, which writes the file; after it, it is an error.
void smith_compile_command(SmithBuild *b, const char *compiler, const char *source, const char *const *flags);
// Writes compile_commands.json at path when build.c runs, only when its bytes change, from every smith_object so far,
// then every smith_compile_command, plus one entry for build.c itself with the command smith compiles it with. Call it
// last.
void smith_compile_commands(SmithBuild *b, const char *path);
