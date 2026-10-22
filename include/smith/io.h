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
// io.processes (spawn and wait), io.net (TLS connections) and io.args. Code can only do what it
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
// process exits wait on kqueue (EVFILT_READ, EVFILT_WRITE, EVFILT_TIMER, EVFILT_PROC). Network connections are
// Network.framework's, which calls back on a dispatch queue; each callback pushes its finished op onto a lock-free list
// and signals the same kqueue (EVFILT_USER), so the ops complete in smith_io_run like every other. Every blocking call
// is an op submitted and run to completion on the same loop.
//
// For tests there is a fake with the same type: smith_io_make_test. Its files are in memory, its console is scripted
// text with the output captured, its clock moves only when told to, its key is given, its servers are scripted, and its
// memory is a buffer you provide. Code under test cannot tell it from the real one.
//
// The project compiles lib/io.c itself (build.c: smith_library(b, "io")), along with the arena and random libraries.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "arena.h"
#include "random.h"

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
    SmithOpKind_PipeWrite,
    SmithOpKind_Timeout,
    SmithOpKind_ProcessWait,
    SmithOpKind_NetConnect,
    SmithOpKind_NetSend,
    SmithOpKind_NetReceive,
    SmithOpKind_NetClose,
} SmithOpKind;

// How smith_files_write puts the bytes in the file.
typedef enum {
    SmithWrite_Replace, // creates the file or replaces what it held
    SmithWrite_Atomic,  // the same, so that nobody sees it half written: the bytes go to "<path>.tmp" and are renamed
                        // over the path, and a failure leaves the old file as it was
    SmithWrite_Append,  // adds size bytes to the end, creating the file if it is not there
    SmithWrite_New,     // creates the file only if there is none: EEXIST if the path is taken, which is how a program
                        // makes a unique name, by trying another
} SmithWrite;

typedef struct [[clang::annotate("smith::capability")]] SmithOp {
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
        void *connection;
        void *child;              // the fake's child, for a child's reads and writes
        struct SmithOp *finished; // network ops: the list of ops the callbacks have finished
        SmithArena *arena;
        const char *path;
        const void *input;
        unsigned char *destination;
        size_t capacity;
        int descriptor;
        int process;
        bool *reaped; // process waits: set when the wait has reaped the process (smith_child_wait_async)
        uint64_t deadline;
        bool ready;
        bool fill;
        SmithWrite mode;
    } internal;
} SmithOp;

// smith_io_run waits at most this long (in nanoseconds); 0 only completes what is ready, and smith_io_forever waits
// for at least one operation.
constexpr int64_t smith_io_forever = -1;

typedef struct [[clang::annotate("smith::capability")]] {
    SmithLoop *loop;
    unsigned char buffer[4096]; // input read from the console and not yet taken
    size_t start;
    size_t end;
} SmithConsole;

typedef struct [[clang::annotate("smith::capability")]] {
    SmithLoop *loop;
} SmithFiles;

typedef struct [[clang::annotate("smith::capability")]] {
    SmithLoop *loop;
} SmithClock;

typedef struct [[clang::annotate("smith::capability")]] {
    SmithLoop *loop;
} SmithProcesses;

typedef struct [[clang::annotate("smith::capability")]] {
    SmithLoop *loop;
} SmithNet;

typedef struct {
    size_t count;
    const char *const *values; // values[count] is nullptr
    const char *const
        *environment; // the fake's "NAME=value" entries, then nullptr; the real io's is nullptr (see below)
} SmithArgs;

typedef struct [[clang::annotate("smith::capability")]] {
    SmithArena arena;
    SmithKey key;  // from getentropy, or from $SMITH_SEED to replay a run
    uint64_t seed; // the key's seed: print it to be able to replay
    SmithConsole console;
    SmithFiles files;
    SmithClock clock;
    SmithProcesses processes;
    SmithNet net;
    SmithArgs args;
    SmithLoop *loop;
} SmithIo;

// The real io. The arena is 64 GiB of address space reserved with mmap; only pages a program touches cost memory (under
// AddressSanitizer the arena poisons lazily, so the shadow grows with what is used). SMITH_SEED, a
// decimal or 0x number, makes the key; otherwise it comes from getentropy. A SMITH_SEED that is not a number, or a
// failure to reserve memory, stops the program with a message.
[[nodiscard, clang::annotate("smith::main_only")]] SmithIo smith_io_make(int argc, char **argv);

// Releases the reservation and the event loop. A program need not call it: the process ends.
void smith_io_close(SmithIo *io);

// Completes the operations that are ready, waiting up to timeout_ns for the first when none is; returns how many it
// completed. File operations always complete here. The fake io never waits: it completes what is ready now, and when
// given a time to wait and nothing is ready it lets that much of its clock pass (that is how it waits) and completes
// what became ready.
size_t smith_io_run(SmithIo *io, int64_t timeout_ns);

// --- errors ---

// The error for an errno value, with the message "what path: strerror".
[[nodiscard]] SmithError smith_error_make(int code, const char *what, const char *path);

// --- files: any path the program is given, absolute or relative; a missing one is ENOENT ---

// The whole file, in the arena.
[[nodiscard]] SmithRead smith_files_read(SmithFiles *files, SmithArena *arena, const char *path);
// Writes size bytes to the file as the mode says.
[[nodiscard]] SmithError smith_files_write(SmithFiles *files, const char *path, const void *data, size_t size,
                                           SmithWrite mode);
void smith_files_read_async(SmithFiles *files, SmithOp *op, SmithArena *arena, const char *path);
void smith_files_write_async(SmithFiles *files, SmithOp *op, const char *path, const void *data, size_t size,
                             SmithWrite mode);

// The calls below run at once and only block: they do not queue behind asynchronous file operations still pending.

// Deletes a file (not a folder: EPERM).
[[nodiscard]] SmithError smith_files_remove(SmithFiles *files, const char *path);
// Makes the folder and the ones above it that are missing; one that is there already is success.
[[nodiscard]] SmithError smith_files_make_folder(SmithFiles *files, const char *path);

typedef struct {
    SmithError error; // ENOENT if there is nothing at the path
    bool folder;
    bool writable; // the program may write here: to a file, or make files in a folder
    size_t size;   // bytes of a file; 0 for a folder
} SmithFileInfo;
// What is at the path (symbolic links followed).
[[nodiscard]] SmithFileInfo smith_files_info(SmithFiles *files, const char *path);

typedef struct {
    const char *name; // the name alone, not the path
    bool folder;
    size_t size; // bytes of a file; 0 for a folder
} SmithFileEntry;

typedef struct {
    SmithError error;
    const SmithFileEntry *entries; // in the arena, sorted by name, without "." and ".."
    size_t count;
} SmithFileList;
// What is in a folder.
[[nodiscard]] SmithFileList smith_files_list(SmithFiles *files, SmithArena *arena, const char *folder);

typedef struct {
    SmithError error;
    void *data; // page-aligned (aligned to smith_arena_align_max in the fake)
    size_t size;
} SmithMapped;
// Maps the file into the address space instead of reading it: nothing is copied or read until the memory is touched,
// so a large file costs only the pages used. The mapping is private: writes change the memory and never the file. It
// lasts as long as the program, and an empty file is EINVAL. The fake gives a copy.
[[nodiscard]] SmithMapped smith_files_map(SmithFiles *files, const char *path);

// --- console ---

// Writes the text to stdout or stderr, all of it or an error.
[[nodiscard]] SmithError smith_console_print(SmithConsole *console, const char *text);
[[nodiscard]] SmithError smith_console_print_error(SmithConsole *console, const char *text);
// printf for the console: formats into a buffer of 4 KiB and prints it. Longer output is EMSGSIZE and prints nothing.
// Not [[nodiscard]], since a program that prints a report has nowhere else to say that stdout is closed.
SmithError smith_console_printf(SmithConsole *console, const char *format, ...) __attribute__((format(printf, 2, 3)));
SmithError smith_console_eprintf(SmithConsole *console, const char *format, ...) __attribute__((format(printf, 2, 3)));
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
// Starts argv[0] with the program's own stdin, stdout and stderr, in this process group, so a signal from the terminal
// reaches it: for a program that talks to the person at the terminal. Without waiting.
[[nodiscard]] SmithError smith_processes_spawn_terminal(SmithProcesses *processes, const char *const *argv,
                                                        SmithProcess *process);
// Completes when the process has exited, with op->exit_status or op->signal.
void smith_processes_wait_async(SmithProcesses *processes, SmithOp *op, SmithProcess process);
// A process id is free to be used again once its wait has completed, so a signal sent to it after that may reach
// another program, and nothing may signal one after its wait. smith_child_kill and smith_capture_stop know it for the
// processes they own; for a SmithProcess of smith_processes_spawn that is the caller's to keep: do not kill an id
// after the wait for it has completed.
// A process whose stdout and stderr are captured together (as `2>&1`, so the lines come in the order the process wrote
// them) while it runs on the loop, so a program can run several at once and print each one's output whole when it ends.
// The caller owns the struct and keeps it where it is until done.
typedef struct [[clang::annotate("smith::capability")]] SmithCapture {
    SmithProcess process; // the process started; it leads a process group of its own, so kill(-process.id, ...) reaches
                          // its children as well
    bool done;            // set by smith_capture_poll: the process exited and its output was read to the end
    SmithError error;     // valid once done: a read failed, or the wait did
    int exit_status;      // 0 if killed
    int signal;           // the signal that killed it, or 0
    const char *output;   // stdout and stderr, NUL-terminated, in the arena given at the start
    size_t output_size;
    bool truncated; // the arena had no room for all of the output: the end of it was read and dropped
    struct {        // private to the library
        SmithLoop *loop;
        SmithArena *arena;
        unsigned char *data;
        size_t capacity;
        int descriptor;
        bool reading;
        bool exited;
        SmithOp read;
        SmithOp wait;
    } internal;
} SmithCapture;
// Starts argv[0] (found on PATH) with no stdin and returns at once; the error is that it could not start (a missing
// program is ENOENT). The output is read in doubling blocks into the arena, so it may use up to twice its size.
[[nodiscard]] SmithError smith_processes_capture_async(SmithProcesses *processes, SmithArena *arena,
                                                       const char *const *argv, SmithCapture *capture);
// Takes in what smith_io_run completed and submits the next read; true once the capture is done. Call it after each
// smith_io_run for every capture still running.
[[nodiscard]] bool smith_capture_poll(SmithCapture *capture);
// Sends the signal to the capture's process group, unless its wait has completed: the id is then free for another
// program, so ESRCH is returned and nothing is sent. That is also the answer once the capture is done. A process that
// has exited but whose output is still being read (a child of it holds the pipe) is therefore not reached either; it
// ends on its own or when the caller gives up on the output.
[[nodiscard]] SmithError smith_capture_stop(SmithCapture *capture, int signal);
// A process started with pipes (smith_processes_start_piped): the program writes to its stdin and reads its stdout, as
// ops on the loop, so one smith_io_run waits on several children's output, the program's own stdin, timers and exits
// together. The caller owns the struct and keeps it where it is until the child is closed. The child leads a process
// group of its own, so smith_child_kill reaches its children too. Its stderr is the program's own, not a pipe: a child
// that logs there can never block on a full pipe nobody reads (and for a server whose stdout is a protocol, the client
// that started it sees the child's log with its own stderr). The pipes carry bytes, not lines: framing is the caller's.
// One write, one read and one wait may be pending on a child at once. Close a descriptor's side
// (smith_child_close_input, smith_child_close) only when no op on it is pending.
typedef struct [[clang::annotate("smith::capability")]] SmithChild {
    SmithProcess process; // wait for it with smith_child_wait_async (smith_processes_wait_async cannot tell
                          // smith_child_kill that the process is gone)
    struct {              // private to the library
        SmithLoop *loop;
        bool reaped; // the wait of smith_child_wait_async has completed: the id may be another program's now
        int input;   // the write end of the child's stdin; -1 once closed
        int output;  // the read end of the child's stdout; -1 once closed
    } internal;
} SmithChild;
// Starts argv[0] (found on PATH) with a pipe for stdin and a pipe for stdout and returns at once. The error is that it
// could not start (a missing program is ENOENT). On the fake, argv[0] needs a script (smith_io_test_add_child) and is
// otherwise ENOENT.
[[nodiscard]] SmithError smith_processes_start_piped(SmithProcesses *processes, const char *const *argv,
                                                     SmithChild *child);
// Completes when all size bytes are in the child's stdin pipe (op->size is size), or fails: EPIPE when the child has
// closed its stdin or exited (op->size says how much was taken before that). SIGPIPE cannot kill the program. The data
// stays the caller's until done. The pipe takes 64 KiB at a time, so a larger write completes as the child reads, and a
// program that also reads its output (smith_child_read_async) cannot deadlock on it.
void smith_child_write_async(SmithChild *child, SmithOp *op, const void *data, size_t size);
// Completes with whatever the child's stdout holds, at least one byte and at most capacity, in buffer (op->data,
// op->size; not NUL-terminated); op->end is true when the child (and every process that kept the pipe) has closed it
// and nothing was read.
void smith_child_read_async(SmithChild *child, SmithOp *op, void *buffer, size_t capacity);
// Completes when the child has exited, with op->exit_status or op->signal, like smith_processes_wait_async; afterwards
// smith_child_kill does not signal the child's id, which is free for another program from then on. The child stays
// where it is until the op is done.
void smith_child_wait_async(SmithChild *child, SmithOp *op);
// Closes the child's stdin, so it sees the end of its input. A child that reads until then, as cat does, exits.
void smith_child_close_input(SmithChild *child);
// Sends the signal to the child's process group. ESRCH if there is nothing left to signal: also, with no signal
// sent, once the wait of smith_child_wait_async has completed, since the id could then be another program's.
[[nodiscard]] SmithError smith_child_kill(SmithChild *child, int signal);
// Closes what is still open of the pipes. It does not kill or wait: wait for the exit first (the pipes close with the
// child anyway), and call this once, after the last op on the child is done.
void smith_child_close(SmithChild *child);
// How many processes can run at once: the machine's logical cores. The fake has what SmithIoTest.cores says, or 1.
[[nodiscard]] size_t smith_processes_cores(const SmithProcesses *processes);

// --- net: TLS connections, from the platform (Network.framework on macOS) ---
//
// A connection is a struct you own and keep where it is until its close has completed. The calls below are ops:
// connect, then send and receive (one of each at a time), then close. An error is a value: refused and unreachable
// hosts are the errno of the system (ECONNREFUSED, EHOSTUNREACH), a name that does not resolve is ENOENT, and a
// certificate or handshake failure is EAUTH with a message that says why ("the certificate is not for this host"). The
// system's own trust decides; there is no way to turn the check off. A connection that failed is closed by the program,
// as one that worked is: close completes every op still pending on it with ECANCELED, and it is the last thing to touch
// the connection, so it may be freed once the close is done.
typedef struct [[clang::annotate("smith::capability")]] SmithConnection {
    struct { // private to the library
        SmithLoop *loop;
        void *handle;
        void *script;
        size_t position;
        _Atomic(struct SmithOp *) waiting[4]; // connect, send, receive, close
        _Atomic bool closed;
        bool end; // the peer has finished sending
        char name[96];
    } internal;
} SmithConnection;

// Opens a connection to host:port; with tls, it is a TLS connection whose certificate is checked against host. The op
// completes when the connection is ready, or fails.
void smith_net_connect_async(SmithNet *net, SmithOp *op, SmithConnection *connection, const char *host, uint16_t port,
                             bool tls);
// Sends all size bytes (the library copies them, so they need not outlive the call).
void smith_net_send_async(SmithNet *net, SmithOp *op, SmithConnection *connection, const void *data, size_t size);
// Completes with whatever has arrived, at least one byte and at most capacity, in buffer (op->data, op->size; not
// NUL-terminated); op->end is true when the peer is done sending and nothing was read.
void smith_net_receive_async(SmithNet *net, SmithOp *op, SmithConnection *connection, void *buffer, size_t capacity);
// Closes the connection.
void smith_net_close_async(SmithNet *net, SmithOp *op, SmithConnection *connection);

// The same, blocking.
[[nodiscard]] SmithError smith_net_connect(SmithNet *net, SmithConnection *connection, const char *host, uint16_t port,
                                           bool tls);
[[nodiscard]] SmithError smith_net_send(SmithNet *net, SmithConnection *connection, const void *data, size_t size);
[[nodiscard]] SmithRead smith_net_receive(SmithNet *net, SmithConnection *connection, void *buffer, size_t capacity);
[[nodiscard]] SmithError smith_net_close(SmithNet *net, SmithConnection *connection);

// --- arguments ---

// The argument i (0 is the program), or nullptr past the end.
[[nodiscard]] const char *smith_args_get(const SmithArgs *args, size_t i);
// The value of an environment variable, or nullptr if it is not set. An empty value is "". The real io reads the
// process's environment each time, so a setenv after smith_io_make shows.
[[nodiscard]] const char *smith_args_env(const SmithArgs *args, const char *name);

// --- the fake io, for tests ---

typedef struct {
    void *memory; // the arena's memory, aligned to smith_arena_align_max
    size_t capacity;
    uint64_t seed;           // the key is smith_key_make(seed)
    const char *input;       // what stdin holds; nullptr is empty. The console reads it, then reports the end
    const char *const *argv; // the arguments, nullptr-terminated; nullptr for none
    size_t argc;
    const char *const *environment; // "NAME=value" entries, nullptr-terminated; nullptr for none
    size_t cores;                   // smith_processes_cores; 0 means 1
} SmithIoTest;

// Same type as the real one. Files, programs and the console's output are inside the arena: the fake holds up to 64
// files and folders and 16 programs, and captures up to 64 KiB of stdout and 64 KiB of stderr (more is ENOSPC).
[[nodiscard]] SmithIo smith_io_make_test(SmithIoTest test);
// Adds an in-memory file (the text is copied), replacing one of the same path.
[[nodiscard]] SmithError smith_io_test_add_file(SmithIo *io, const char *path, const char *text);
// Scripts a program for smith_processes_run and spawn: its stdout and its exit status.
[[nodiscard]] SmithError smith_io_test_add_program(SmithIo *io, const char *program, const char *output, int status);
// When a scripted child (smith_io_test_add_child) has exited.
typedef enum {
    SmithChildExit_Never,       // never by itself: only smith_child_kill ends it (a debugger waiting for requests)
    SmithChildExit_Start,       // at once, like `true`: its output can be read, and a write is EPIPE
    SmithChildExit_InputClosed, // when its stdin is closed, like `cat`
    SmithChildExit_OutputRead,  // when all its output has been read
} SmithChildExit;
// A child of the fake io, for smith_processes_start_piped: the first script of the program that no child has used yet
// is the one started (so two children of one program are two scripts). The child writes output to its stdout in pieces
// of at most chunk bytes when chunk is not 0, from the start; a read waits (never completes, so the clock moves in
// smith_io_run) while it has written all it will and has not exited, and ends after the exit. A wait completes at the
// exit, with status, or with the signal of smith_child_kill. Writes are kept (smith_io_test_child_input), up to 8 KiB;
// a write to an exited child is EPIPE. Up to 16 children.
typedef struct {
    const char *program;
    const char *output;
    size_t chunk;
    SmithChildExit exit_on;
    int status;
} SmithChildScript;
[[nodiscard]] SmithError smith_io_test_add_child(SmithIo *io, SmithChildScript script);
// The bytes written to the n-th scripted child's stdin (n counts the scripts added, from 0), NUL-terminated; nullptr if
// there is no such child.
[[nodiscard]] const char *smith_io_test_child_input(const SmithIo *io, size_t n);
// What the program printed to stdout and stderr so far, NUL-terminated.
[[nodiscard]] const char *smith_io_test_output(const SmithIo *io);
[[nodiscard]] const char *smith_io_test_errors(const SmithIo *io);

// A scripted server for the net: a connection to host:port is served by the first server added for it that no
// connection has used yet, so a redirect to another host is a second server. The server sends response, in pieces of at
// most chunk bytes when chunk is not 0, and then either closes cleanly or, when error is not 0, fails the next receive
// with that errno. With tls_failure the connect fails with EAUTH and that message instead. A connection to a host with
// no server is ECONNREFUSED. Once any server is added the real io is scripted too: its files, console and clock stay
// real, and no connection goes out to the network. This is how a test of code that downloads runs on a real disk. Up to
// 64 servers.
typedef struct {
    const char *host;
    uint16_t port;
    const void *response;
    size_t response_size;
    size_t chunk;
    int error;
    const char *tls_failure;
    bool
        stall; // after the response, a receive never completes: the server has gone quiet (a closed connection ends it)
} SmithNetServer;
[[nodiscard]] SmithError smith_io_test_add_server(SmithIo *io, SmithNetServer server);
// The bytes the client sent on the n-th connection (from 0) to host:port, NUL-terminated, up to 8 KiB; nullptr if there
// was no such connection.
[[nodiscard]] const char *smith_io_test_request(const SmithIo *io, const char *host, uint16_t port, size_t n);
