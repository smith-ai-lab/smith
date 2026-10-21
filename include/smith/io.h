// <smith/io.h>: everything a program does to the world outside it, as values you hold and pass on. One line in main
// makes all of it:
//
//   int main(int argc, char **argv) {
//       SmithIo io = smith_io_make(argc, argv);
//       ...
//   }
//
// io holds every capability the program has, as a field: io.arena (memory), io.key and io.seed (randomness), io.console
// (stdin, stdout, stderr), io.files (the file system), io.clock (monotonic and wall time),
// io.processes (spawn and wait) and io.args. The network comes in the next version as io.net. Code can only do what it
// was handed: a function that is not given a clock cannot depend on time, and one that is not given files cannot touch
// the disk.
//
// A function takes the narrowest thing that covers what it uses.
//   One or two capabilities: those alone.
//     Mesh mesh_load(SmithFiles *files, SmithArena *arena, const char *path);
//     Mesh mesh = mesh_load(&io.files, &io.arena, "cube.obj");        // in main
//   Most of io: the whole of it.
//     void game_run(SmithIo *io);
//   A bundle several functions share: a struct the project defines, of the capabilities that group needs.
//     typedef struct { SmithFiles *files; SmithConsole *console; SmithArena *arena; } Editor;
//     Editor editor = {&io.files, &io.console, &io.arena};
//     editor_open(&editor, path);
// If a signature wants four or more separate capabilities, take SmithIo * or define the struct. The pointers point into
// the io in main, so keep that one in place for as long as they are used.
//
// Errors are values. Every operation reports success or a SmithError with a message; nothing is printed behind your
// back, and a function that can fail says so in its result. Only what the program cannot continue without stops it:
// smith_io_make failing to reserve memory, and (as in the arena) a push past the arena's capacity.
//
// Blocking calls are for simple code:
//   SmithRead text = smith_files_read(&io.files, &io.arena, "config.txt");
//   if (text.error.code != 0) { smith_console_print_error(&io.console, text.error.message); return 1; }
//   for (size_t turn = 0; turn < turn_max; turn++) {          // reading until the input ends
//       SmithRead line = smith_console_read_line(&io.console, &io.arena);
//       if (line.end) break;
//       ...
//   }
//
// Asynchronous calls are for waiting on several things at once. You submit an operation in a SmithOp you own and keep
// alive, then smith_io_run completes what is ready, marking each op done:
//   SmithOp timer = {}, wait = {};
//   smith_clock_timeout_async(&io.clock, &timer, 2'000'000'000);
//   smith_processes_wait_async(&io.processes, &wait, child);
//   for (size_t i = 0; i < rounds_max && !(timer.done && wait.done); i++)
//       smith_io_run(&io, smith_io_forever);
// Completion is a done flag, not a callback. Everything the program needs is in the op (error, data, status), a loop in
// the caller's own control flow reads it where the state lives, no closure or context pointer is needed, and an
// io_uring or IOCP backend can complete the same struct by its user data. An op must not move or be freed while it is
// pending, and may be reused after it is done. Names, paths and data an op points at belong to the caller until done.
//
// Underneath is one kqueue event loop. macOS has no asynchronous file IO, so file operations run synchronously, in
// the order they were submitted, inside smith_io_run: submitting only queues them. Pipes, the console, timers and
// process exits wait on kqueue (EVFILT_READ, EVFILT_TIMER, EVFILT_PROC). Every blocking call is an op submitted and run
// to completion on the same loop.
//
// For tests there is a fake with the same type: smith_io_make_test. Its files are in memory, its console is scripted
// text with the output captured, its clock moves only when told to, its key is given, and its memory is a buffer you
// provide. Code under test cannot tell it from the real one.
//
// The project compiles lib/io.c itself (build.c: smith_library(b, "io")), along with the arena and random libraries.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <smith/arena.h>
#include <smith/random.h>

// An error: code is 0 for success, otherwise an errno value (ENOENT, EINVAL...), and message says what failed, such
// as "open config.txt: No such file or directory". Real and fake io give the same code and message.
typedef struct {
    int code;
    char message[192];
} SmithError;

// The result of reading: data is NUL-terminated (size does not count the NUL), in the arena you gave. For a read of the
// console, end is true when the input is over and nothing was read.
typedef struct {
    SmithError error;
    const char *data;
    size_t size;
    bool end;
} SmithRead;

// The caller owns an op and keeps it where it is until done. Fill it only through the *_async functions. Reuse after
// done is allowed.
typedef struct SmithLoop SmithLoop;
typedef enum {
    SmithOpKind_None,
    SmithOpKind_FileRead,
    SmithOpKind_FileWrite,
    SmithOpKind_ConsoleRead,
    SmithOpKind_PipeRead,
    SmithOpKind_Timeout,
    SmithOpKind_ProcessWait,
} SmithOpKind;

typedef struct SmithOp {
    bool done;        // set by smith_io_run when the op finished, successfully or not
    SmithError error; // valid once done
    const char *data; // reads: NUL-terminated, in the arena given at submit
    size_t size;      // reads: bytes read
    bool end;         // console reads: the input is over
    int exit_status;  // process waits: the exit status, or 0 if killed
    int signal;       // process waits: the signal that killed it, or 0
    struct {          // private to the library
        SmithOpKind kind;
        struct SmithOp *next;
        SmithLoop *loop;
        void *console;
        SmithArena *arena;
        const char *path;
        const void *input;
        unsigned char *destination;
        size_t capacity;
        int descriptor;
        int process;
        uint64_t deadline;
        bool ready;
        bool fill;
    } internal;
} SmithOp;

// smith_io_run waits at most this long (in nanoseconds); 0 only completes what is ready, and smith_io_forever waits
// for at least one operation.
constexpr int64_t smith_io_forever = -1;

typedef struct {
    SmithLoop *loop;
    unsigned char buffer[4096]; // input read from the console and not yet taken
    size_t start;
    size_t end;
} SmithConsole;

typedef struct {
    SmithLoop *loop;
} SmithFiles;

typedef struct {
    SmithLoop *loop;
} SmithClock;

typedef struct {
    SmithLoop *loop;
} SmithProcesses;

typedef struct {
    size_t count;
    const char *const *values; // values[count] is nullptr
} SmithArgs;

typedef struct {
    SmithArena arena;
    SmithKey key;  // from getentropy, or from $SMITH_SEED to replay a run
    uint64_t seed; // the key's seed: print it to be able to replay
    SmithConsole console;
    SmithFiles files;
    SmithClock clock;
    SmithProcesses processes;
    SmithArgs args;
    // net comes in the next version
    SmithLoop *loop;
} SmithIo;

// The real io. The arena is 64 GiB of address space reserved with mmap; only pages a program touches cost memory (under
// AddressSanitizer the arena poisons lazily, so the shadow grows with what is used). SMITH_SEED, a
// decimal or 0x number, makes the key; otherwise it comes from getentropy. A SMITH_SEED that is not a number, or a
// failure to reserve memory, stops the program with a message.
[[nodiscard]] SmithIo smith_io_make(int argc, char **argv);

// Releases the reservation and the event loop. A program need not call it: the process ends.
void smith_io_close(SmithIo *io);

// Completes the operations that are ready, waiting up to timeout_ns for the first when none is; returns how many it
// completed. File operations always complete here. The fake io never waits: it completes what is ready now.
size_t smith_io_run(SmithIo *io, int64_t timeout_ns);

// --- errors ---

// The error for an errno value, with the message "what path: strerror".
[[nodiscard]] SmithError smith_error_make(int code, const char *what, const char *path);

// --- files: any path the program is given, absolute or relative; a missing one is ENOENT ---

// The whole file, in the arena.
[[nodiscard]] SmithRead smith_files_read(SmithFiles *files, SmithArena *arena, const char *path);
// Creates or replaces the file with size bytes.
[[nodiscard]] SmithError smith_files_write(SmithFiles *files, const char *path, const void *data, size_t size);
void smith_files_read_async(SmithFiles *files, SmithOp *op, SmithArena *arena, const char *path);
void smith_files_write_async(SmithFiles *files, SmithOp *op, const char *path, const void *data, size_t size);

// --- console ---

// Writes the text to stdout or stderr, all of it or an error.
[[nodiscard]] SmithError smith_console_print(SmithConsole *console, const char *text);
[[nodiscard]] SmithError smith_console_print_error(SmithConsole *console, const char *text);
// The next line of stdin without its newline, in the arena. At the end of the input end is true; a last line with no
// newline is still returned first.
[[nodiscard]] SmithRead smith_console_read_line(SmithConsole *console, SmithArena *arena);
// Completes with whatever is available, at most max bytes, in the arena; size 0 and end set mean the input is over.
void smith_console_read_async(SmithConsole *console, SmithOp *op, SmithArena *arena, size_t max);

// --- clock ---

// Monotonic nanoseconds: they never go back, and mean nothing but as a difference.
[[nodiscard]] uint64_t smith_clock_now(const SmithClock *clock);
// Nanoseconds since the Unix epoch.
[[nodiscard]] int64_t smith_clock_wall(const SmithClock *clock);
// Completes after ns nanoseconds of the clock. On the fake clock that is when it has been advanced that far.
void smith_clock_timeout_async(SmithClock *clock, SmithOp *op, uint64_t ns);
// Blocks for ns. On the fake clock nothing can advance it, so this is the error EDEADLK unless ns is 0.
[[nodiscard]] SmithError smith_clock_sleep(SmithClock *clock, uint64_t ns);
// Moves the fake clock forward, and only the fake clock (asserted). Both the monotonic and the wall time move.
void smith_clock_advance(SmithClock *clock, uint64_t ns);

// --- processes ---

typedef struct {
    int id; // a process id; a negative id is a process of the fake io
} SmithProcess;

typedef struct {
    SmithError error;   // could not start (a missing program is ENOENT); otherwise success even if it failed
    int exit_status;    // 0 if killed
    int signal;         // the signal that killed it, or 0
    const char *output; // stdout, NUL-terminated, in the arena
    size_t output_size;
    const char *errors; // stderr
    size_t errors_size;
} SmithProcessResult;

// Runs argv[0] (found on PATH) with no stdin and waits, capturing stdout and stderr in the arena. The capture is
// read in doubling blocks, so it may use up to twice the output's size.
[[nodiscard]] SmithProcessResult smith_processes_run(SmithProcesses *processes, SmithArena *arena,
                                                     const char *const *argv);
// Starts argv[0] with no stdin and the program's own stdout and stderr, without waiting.
[[nodiscard]] SmithError smith_processes_spawn(SmithProcesses *processes, const char *const *argv,
                                               SmithProcess *process);
// Completes when the process has exited, with op->exit_status or op->signal.
void smith_processes_wait_async(SmithProcesses *processes, SmithOp *op, SmithProcess process);

// --- arguments ---

// The argument i (0 is the program), or nullptr past the end.
[[nodiscard]] const char *smith_args_get(const SmithArgs *args, size_t i);

// --- the fake io, for tests ---

typedef struct {
    void *memory; // the arena's memory, aligned to smith_arena_align_max
    size_t capacity;
    uint64_t seed;           // the key is smith_key_make(seed)
    const char *input;       // what stdin holds; nullptr is empty. The console reads it, then reports the end
    const char *const *argv; // the arguments, nullptr-terminated; nullptr for none
    size_t argc;
} SmithIoTest;

// Same type as the real one. Files, programs and the console's output are inside the arena: the fake holds up to 64
// files and 16 programs, and captures up to 64 KiB of stdout and 64 KiB of stderr (more is ENOSPC).
[[nodiscard]] SmithIo smith_io_make_test(SmithIoTest test);
// Adds an in-memory file (the text is copied), replacing one of the same path.
[[nodiscard]] SmithError smith_io_test_add_file(SmithIo *io, const char *path, const char *text);
// Scripts a program for smith_processes_run and spawn: its stdout and its exit status.
[[nodiscard]] SmithError smith_io_test_add_program(SmithIo *io, const char *program, const char *output, int status);
// What the program printed to stdout and stderr so far, NUL-terminated.
[[nodiscard]] const char *smith_io_test_output(const SmithIo *io);
[[nodiscard]] const char *smith_io_test_errors(const SmithIo *io);
