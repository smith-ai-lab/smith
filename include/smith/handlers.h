// The handlers of the runtime that every smith program links (runtime/handlers.c): the allocator of the current thread,
// which malloc, calloc, realloc, free, posix_memalign, aligned_alloc, valloc, malloc_size and the malloc_zone_ family
// reach, from smith code, from a prebuilt library and from one loaded with dlopen. No allocator set (the default) is
// libc's. Issue #239; knowledge/handlers.md. Plain C: a C++ file includes it inside extern "C" { } (the grammar for
// .smith, which parses smith's hand-written C, does not read the closing of such a block in a header).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct SmithAllocator SmithAllocator;

// An allocator is a struct whose first member is a SmithAllocator, passed as &x.allocator. `owns` says whether a block
// came from it (a free or realloc of any other block goes to libc); `size` is the usable size of a block it owns, and
// may be null (malloc_size then says 0 for its blocks).
struct SmithAllocator {
    void *(*alloc)(SmithAllocator *self, size_t size, size_t align);
    void *(*realloc)(SmithAllocator *self, void *block, size_t size, size_t align);
    void (*free)(SmithAllocator *self, void *block);
    bool (*owns)(SmithAllocator *self, const void *block);
    size_t (*size)(SmithAllocator *self, const void *block);
};

// Sets the allocator of the calling thread (null: libc's) and returns the one it had, as signal() does; restoring it is
// the caller's, with `defer`.
SmithAllocator *smith_handlers_set_allocator(SmithAllocator *allocator);

// The allocator of the calling thread, null when none is set.
SmithAllocator *smith_handlers_allocator(void);

// The files of the calling thread: open, read, write and close of a file descriptor, which open, read, write, close,
// syscall (SYS_read, SYS_write) and, through funopen, fopen, fdopen and freopen reach while one is set. Every
// descriptor from 3 up goes to it (a fake decides what its descriptors are); 0, 1 and 2 are the console's. No files
// handler set (the default) is libc's. A stream a handler opened is not seekable (the handler has no seek).
typedef struct SmithFileHandler SmithFileHandler;

typedef struct {
    bool folder;
    bool writable; // the program may write here: to a file, or make files in a folder
    size_t size;   // bytes of a file; 0 for a folder
} SmithFileStatus;

typedef struct {
    char name[256]; // the name alone, not the path
    bool folder;
    size_t size; // bytes of a file; 0 for a folder
} SmithFileItem;

struct SmithFileHandler {
    int (*open)(SmithFileHandler *self, const char *path, int flags, int mode);
    ssize_t (*read)(SmithFileHandler *self, int fd, void *buffer, size_t size);
    ssize_t (*write)(SmithFileHandler *self, int fd, const void *buffer, size_t size);
    int (*close)(SmithFileHandler *self, int fd);
    // The members below are called by std::files (lib/std/files.smith), not by an interposed libc function; each
    // may be null, which is libc's own call (stat, mkdir, unlink, rename, opendir, mmap, munmap). They return 0 for
    // success and -1 with errno set, as the libc calls do.
    int (*stat)(SmithFileHandler *self, const char *path, SmithFileStatus *status);
    int (*mkdir)(SmithFileHandler *self, const char *path);  // one folder; EEXIST if it is there
    int (*remove)(SmithFileHandler *self, const char *path); // a file
    int (*rename)(SmithFileHandler *self, const char *from, const char *to);
    // The index-th entry of a folder, without "." and "..", in any order: 1 when item is filled, 0 past the last, -1 on
    // an error.
    int (*list)(SmithFileHandler *self, const char *folder, size_t index, SmithFileItem *item);
    // Maps a file (private, writable): the memory and its size, or null with errno set.
    void *(*map)(SmithFileHandler *self, const char *path, size_t *size);
    void (*unmap)(SmithFileHandler *self, void *data, size_t size);
};

// Sets the files of the calling thread (null: libc's), returning the ones it had.
SmithFileHandler *smith_handlers_set_files(SmithFileHandler *files);

// The files of the calling thread, null when none is set.
SmithFileHandler *smith_handlers_files(void);

// The console of the calling thread: what is written to stdout and stderr (fd 1 and 2, whichever way: write, fwrite,
// printf, puts, dprintf...) and what is read from stdin (read on fd 0, fgets, getchar, scanf...). No console handler
// set is libc's.
typedef struct SmithConsoleHandler SmithConsoleHandler;

struct SmithConsoleHandler {
    ssize_t (*write)(SmithConsoleHandler *self, int fd, const void *buffer, size_t size); // fd is 1 or 2
    ssize_t (*read)(SmithConsoleHandler *self, void *buffer, size_t size);
};

// Sets the console of the calling thread (null: libc's), returning the one it had.
SmithConsoleHandler *smith_handlers_set_console(SmithConsoleHandler *console);

// The console of the calling thread, null when none is set.
SmithConsoleHandler *smith_handlers_console(void);

// The environment of the calling thread: getenv reaches it while one is set. `get` returns the value of a variable, or
// null when it is not set; the handler owns the string. No env handler set is libc's.
typedef struct SmithEnvHandler SmithEnvHandler;

struct SmithEnvHandler {
    char *(*get)(SmithEnvHandler *self, const char *name);
};

// Sets the environment of the calling thread (null: libc's), returning the one it had.
SmithEnvHandler *smith_handlers_set_env(SmithEnvHandler *env);

// The environment of the calling thread, null when none is set.
SmithEnvHandler *smith_handlers_env(void);

// The clock of the calling thread: the time clock_gettime reads and the sleeping that nanosleep, usleep and sleep do.
// `now` is the time of the clock `which` (CLOCK_MONOTONIC, CLOCK_REALTIME...) in nanoseconds; `sleep` waits for that
// many nanoseconds (a fake advances its time and returns at once, so it never blocks and never fails). No clock handler
// set is libc's. gettimeofday, time and the Mach clocks are not reached.
typedef struct SmithClockHandler SmithClockHandler;

struct SmithClockHandler {
    int64_t (*now)(SmithClockHandler *self, int which);
    void (*sleep)(SmithClockHandler *self, uint64_t nanoseconds);
};

// Sets the clock of the calling thread (null: libc's), returning the one it had.
SmithClockHandler *smith_handlers_set_clock(SmithClockHandler *clock);

// The clock of the calling thread, null when none is set.
SmithClockHandler *smith_handlers_clock(void);
// The child processes of the calling thread: std::process (lib/std/process.smith) calls these members itself, with
// no libc function behind them, so a thread with a handler never starts a real process. A child is an id the handler
// chooses (greater than 0). Calls return 0 (or a count) for success and -1 with errno set, as the libc calls do. No
// process handler set is the real posix_spawn, pipes and waitpid. Issue #240; knowledge/process.md.
typedef struct SmithProcessHandler SmithProcessHandler;

// How a child's standard streams are connected.
enum SmithProcessMode {
    SmithProcessMode_Inherit = 0, // no stdin; stdout and stderr are the program's own
    SmithProcessMode_Piped = 1,   // a pipe to its stdin and from its stdout; stderr is the program's own
    SmithProcessMode_Captured = 2 // no stdin; pipes from its stdout and from its stderr (stream 1 and 2 of read)
};

struct SmithProcessHandler {
    // Starts argv[0] (a null-terminated array, found on PATH) connected as `mode` says; the id of the child, or -1.
    int (*start)(SmithProcessHandler *self, const char *const *argv, int mode);
    // Reads up to size bytes of the child's stream (1: stdout, 2: stderr) into buffer, blocking until there is
    // something: the count, 0 at the end of the stream, -1 on an error.
    ssize_t (*read)(SmithProcessHandler *self, int child, int stream, void *buffer, size_t size);
    // Writes all of size bytes to the child's stdin: the count, or -1 (EPIPE when the child has exited or closed it).
    ssize_t (*write)(SmithProcessHandler *self, int child, const void *buffer, size_t size);
    // Closes the child's stdin, so it sees the end of its input.
    int (*close_input)(SmithProcessHandler *self, int child);
    // Blocks until the child has exited: *status is its exit status (0 if killed) and *signal the signal that killed it
    // (0 if not).
    int (*wait)(SmithProcessHandler *self, int child, int *status, int *signal);
    // Sends the signal to the child and the processes of its group; ESRCH when it has exited.
    int (*kill)(SmithProcessHandler *self, int child, int signal);
    // Releases the child's streams. Called once; the handler may refuse a second time.
    int (*close)(SmithProcessHandler *self, int child);
    // How many processes can run at once; null means the machine's logical cores.
    size_t (*cores)(SmithProcessHandler *self);
};

// Sets the processes of the calling thread (null: the real ones), returning the ones it had.
SmithProcessHandler *smith_handlers_set_process(SmithProcessHandler *process);

// The processes of the calling thread, null when the real ones are in force.
SmithProcessHandler *smith_handlers_process(void);

// The network of the calling thread: std::net (lib/std/net.smith) calls these members itself, with no libc function
// behind them, so a thread with a handler never opens a real socket. A connection or a listener is an id the handler
// chooses (greater than 0). Calls return an id, a count or 0 for success and -1 with errno set, as the libc calls do.
// No net handler set is the real sockets, with TLS from the system's stack. Issue #240; knowledge/net.md.
typedef struct SmithNetHandler SmithNetHandler;

// Why a TLS connection was refused: `connect` sets *reason to one of these (it is 0 otherwise) and errno to EPROTO.
enum SmithNetworkTls {
    SmithNetworkTls_Failed = 1,    // the handshake failed, for a reason not named below
    SmithNetworkTls_Expired = 2,   // the certificate has expired
    SmithNetworkTls_NotYet = 3,    // the certificate is not valid yet
    SmithNetworkTls_Host = 4,      // the certificate is not for this host
    SmithNetworkTls_Untrusted = 5, // self-signed, or its root is not trusted
    SmithNetworkTls_Invalid = 6    // the chain is not valid, or the certificate was rejected
};

struct SmithNetHandler {
    // Connects to host (a name or an address) and port, with TLS when `tls`: the id of the connection, or -1 (errno
    // EPROTO with *reason set when the certificate or the handshake was refused).
    int (*connect)(SmithNetHandler *self, const char *host, unsigned short port, bool tls, int *reason);
    // Listens on host (null: every address) and port (0: one the system chooses): the id of the listener, or -1.
    int (*listen)(SmithNetHandler *self, const char *host, unsigned short port);
    // The port a listener is bound to.
    int (*port)(SmithNetHandler *self, int listener);
    // Blocks until a client has connected to the listener: the id of the connection, or -1.
    int (*accept)(SmithNetHandler *self, int listener);
    // Reads up to size bytes, blocking until there is something: the count, 0 when the peer has finished, -1 on an
    // error.
    ssize_t (*read)(SmithNetHandler *self, int connection, void *buffer, size_t size);
    // Writes up to size bytes: the count (maybe less than size), or -1 (EPIPE when the peer has closed).
    ssize_t (*write)(SmithNetHandler *self, int connection, const void *buffer, size_t size);
    // Closes a connection or a listener. Called once; the handler may refuse a second time.
    int (*close)(SmithNetHandler *self, int id);
};

// Sets the network of the calling thread (null: the real one), returning the one it had.
SmithNetHandler *smith_handlers_set_net(SmithNetHandler *net);

// The network of the calling thread, null when the real one is in force.
SmithNetHandler *smith_handlers_net(void);

// The entropy of the calling thread: the random bytes getentropy fills (smith::random::seed asks it). `fill` writes
// `size` bytes of buffer and never fails (a fake writes the bytes of a number it holds, so a test scripts the seed). No
// entropy handler set is the system's. arc4random and getrandom are not reached.
typedef struct SmithEntropyHandler SmithEntropyHandler;

struct SmithEntropyHandler {
    void (*fill)(SmithEntropyHandler *self, unsigned char *buffer, size_t size);
};

// Sets the entropy of the calling thread (null: the system's), returning the one it had.
SmithEntropyHandler *smith_handlers_set_entropy(SmithEntropyHandler *entropy);

// The entropy of the calling thread, null when none is set.
SmithEntropyHandler *smith_handlers_entropy(void);
