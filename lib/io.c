#include <smith/io.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef __APPLE__
#error "<smith/io.h> is implemented for macOS only; Linux (io_uring) and Windows (IOCP) come later"
#endif

extern char **environ;

// An AddressSanitizer build commits a shadow byte for every 8 bytes the arena poisons, so it reserves far less.
#if __has_feature(address_sanitizer)
constexpr size_t smith_io_reservation = (size_t)1 << 30;
#else
constexpr size_t smith_io_reservation = (size_t)64 << 30;
#endif

constexpr size_t smith_io_fake_files_max = 64;
constexpr size_t smith_io_fake_programs_max = 16;
constexpr size_t smith_io_fake_capture = (size_t)64 << 10;
constexpr size_t smith_io_events_max = 16;
constexpr size_t smith_io_chunk = 4096;
constexpr int smith_io_file_mode = 0b110100100; // rw-r--r--, before the umask
constexpr uint64_t smith_io_nanoseconds = 1'000'000'000;

typedef struct {
    const char *path;
    unsigned char *data;
    size_t size;
} SmithFakeFile;

typedef struct {
    const char *program;
    const char *output;
    int status;
} SmithFakeProgram;

typedef struct {
    SmithArena storage; // a quarter of the test memory: file contents, names and scripts
    SmithFakeFile files[smith_io_fake_files_max];
    size_t file_count;
    SmithFakeProgram programs[smith_io_fake_programs_max];
    size_t program_count;
    const char *input;
    size_t input_size;
    size_t input_position;
    char *output;
    size_t output_size;
    char *errors;
    size_t errors_size;
    uint64_t monotonic;
    int64_t wall;
} SmithFake;

struct SmithLoop {
    int queue; // the kqueue, -1 in the fake
    SmithFake *fake;
    SmithOp *head;
    SmithOp *tail;
    size_t pending;
    void *memory; // the reservation, to release
    size_t memory_size;
};

// ---------------------------------------------------------------- errors

SmithError smith_error_make(int code, const char *what, const char *path) {
    assert(what != nullptr && path != nullptr);
    SmithError error = {.code = code};
    if (code == 0) return error;
    char reason[96] = "";
    if (strerror_r(code, reason, sizeof reason) != 0) snprintf(reason, sizeof reason, "error %d", code);
    [[maybe_unused]] int written = snprintf(error.message, sizeof error.message, "%s %s: %s", what, path, reason);
    assert(written > 0);
    return error;
}

[[noreturn]] static void smith_io_stop(const char *message) {
    fprintf(stderr, "smith: io: %s\n", message);
    fflush(stderr);
    abort();
}

// ---------------------------------------------------------------- growing buffers

// A block in the arena that doubles when full (the old block stays behind), for output of unknown size.
typedef struct {
    SmithArena *arena;
    unsigned char *data;
    size_t size;
    size_t capacity;
} SmithGrow;

// Makes room for at least `extra` more bytes and a NUL.
static void smith_grow_reserve(SmithGrow *grow, size_t extra) {
    assert(grow->arena != nullptr && grow->size <= grow->capacity);
    if (grow->data != nullptr && grow->capacity - grow->size > extra) return;
    size_t wanted = grow->capacity * 2;
    if (wanted < grow->size + extra + 1) wanted = grow->size + extra + 1;
    if (wanted < 256) wanted = 256;
    unsigned char *bigger = smith_arena_push(grow->arena, wanted, 1);
    if (grow->size > 0) memcpy(bigger, grow->data, grow->size);
    grow->data = bigger;
    grow->capacity = wanted;
}

static void smith_grow_append(SmithGrow *grow, const void *bytes, size_t size) {
    smith_grow_reserve(grow, size);
    if (size > 0) memcpy(grow->data + grow->size, bytes, size);
    grow->size += size;
    grow->data[grow->size] = 0;
}

// ---------------------------------------------------------------- paths and descriptors

static SmithError smith_path_check(const char *path) {
    SmithError error = {};
    bool bad = path == nullptr || path[0] == '\0' || path[0] == '/';
    for (const char *c = path; !bad && *c != '\0'; c++) { // bounded by the path's length
        bool component_start = c == path || c[-1] == '/';
        if (component_start && c[0] == '.' && c[1] == '.' && (c[2] == '/' || c[2] == '\0')) bad = true;
    }
    if (bad) {
        error.code = EINVAL;
        snprintf(error.message, sizeof error.message, "path %s: must be relative to the current folder, with no ..",
                 path == nullptr ? "(null)" : path);
    }
    return error;
}

// Writes all size bytes. Each pass writes at least one byte or fails, so the loop is bounded by size.
static int smith_write_all(int descriptor, const void *data, size_t size) {
    const unsigned char *bytes = data;
    size_t done = 0;
    for (size_t passes = 0; done < size && passes <= size + 64; passes++) {
        ssize_t n = write(descriptor, bytes + done, size - done);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return errno;
        done += (size_t)n;
    }
    return done == size ? 0 : EIO;
}

// ---------------------------------------------------------------- the fake

static SmithFake *smith_fake_of(SmithLoop *loop) {
    assert(loop != nullptr);
    return loop->fake;
}

static SmithFakeFile *smith_fake_find(SmithFake *fake, const char *path) {
    for (size_t i = 0; i < fake->file_count; i++)
        if (strcmp(fake->files[i].path, path) == 0) return &fake->files[i];
    return nullptr;
}

static SmithError smith_fake_capture(SmithFake *fake, bool errors, const char *text) {
    char *buffer = errors ? fake->errors : fake->output;
    size_t *size = errors ? &fake->errors_size : &fake->output_size;
    size_t length = strlen(text);
    if (length > smith_io_fake_capture - 1 - *size) {
        SmithError error = {.code = ENOSPC};
        snprintf(error.message, sizeof error.message, "write console: the test io captures at most %zu bytes",
                 smith_io_fake_capture - 1);
        return error;
    }
    memcpy(buffer + *size, text, length);
    *size += length;
    buffer[*size] = 0;
    return (SmithError){};
}

static SmithError smith_fake_read_file(SmithFake *fake, SmithArena *arena, const char *path, SmithRead *result) {
    SmithFakeFile *file = smith_fake_find(fake, path);
    if (file == nullptr) return smith_error_make(ENOENT, "open", path);
    unsigned char *copy = smith_arena_push(arena, file->size + 1, 1);
    if (file->size > 0) memcpy(copy, file->data, file->size);
    result->data = (const char *)copy;
    result->size = file->size;
    return (SmithError){};
}

static SmithError smith_fake_write_file(SmithFake *fake, const char *path, const void *data, size_t size) {
    SmithFakeFile *file = smith_fake_find(fake, path);
    if (file == nullptr) {
        if (fake->file_count == smith_io_fake_files_max) return smith_error_make(ENOSPC, "create", path);
        file = &fake->files[fake->file_count++];
        char *name = smith_arena_push(&fake->storage, strlen(path) + 1, 1);
        memcpy(name, path, strlen(path));
        file->path = name;
    }
    file->data = smith_arena_push(&fake->storage, size + 1, 1);
    if (size > 0) memcpy(file->data, data, size);
    file->size = size;
    return (SmithError){};
}

// ---------------------------------------------------------------- real file operations

static SmithError smith_real_read_file(SmithArena *arena, const char *path, SmithRead *result) {
    int descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) return smith_error_make(errno, "open", path);
    struct stat info = {};
    SmithGrow grow = {.arena = arena};
    if (fstat(descriptor, &info) == 0 && info.st_size > 0) smith_grow_reserve(&grow, (size_t)info.st_size);
    SmithError error = {};
    for (size_t rounds = 0; rounds <= arena->capacity; rounds++) { // each round reads a byte or ends
        smith_grow_reserve(&grow, smith_io_chunk);
        ssize_t n = read(descriptor, grow.data + grow.size, grow.capacity - grow.size - 1);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) error = smith_error_make(errno, "read", path);
        if (n <= 0) break;
        grow.size += (size_t)n;
    }
    close(descriptor);
    smith_grow_reserve(&grow, 0);
    grow.data[grow.size] = 0;
    result->data = (const char *)grow.data;
    result->size = grow.size;
    return error;
}

static SmithError smith_real_write_file(const char *path, const void *data, size_t size) {
    int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, smith_io_file_mode);
    if (descriptor < 0) return smith_error_make(errno, "open", path);
    int code = smith_write_all(descriptor, data, size);
    if (close(descriptor) != 0 && code == 0) code = errno;
    return smith_error_make(code, "write", path);
}

// ---------------------------------------------------------------- the console's input

// Reads up to capacity bytes from stdin (or the fake's text) into destination; 0 is the end.
static ssize_t smith_console_source(SmithLoop *loop, unsigned char *destination, size_t capacity, int *code) {
    if (loop->fake == nullptr) {
        for (size_t tries = 0; tries < 64; tries++) { // a signal may interrupt the read
            ssize_t n = read(STDIN_FILENO, destination, capacity);
            if (n >= 0 || errno != EINTR) {
                *code = n < 0 ? errno : 0;
                return n;
            }
        }
        *code = EINTR;
        return -1;
    }
    SmithFake *fake = loop->fake;
    size_t left = fake->input_size - fake->input_position;
    size_t n = left < capacity ? left : capacity;
    if (n > 0) memcpy(destination, fake->input + fake->input_position, n);
    fake->input_position += n;
    *code = 0;
    return (ssize_t)n;
}

// ---------------------------------------------------------------- the loop

static void smith_loop_link(SmithLoop *loop, SmithOp *op) {
    op->internal.next = nullptr;
    if (loop->tail == nullptr)
        loop->head = op;
    else
        loop->tail->internal.next = op;
    loop->tail = op;
    loop->pending += 1;
}

static void smith_loop_unlink(SmithLoop *loop, SmithOp *op) {
    SmithOp *previous = nullptr;
    SmithOp *at = loop->head;
    for (size_t i = 0; i < loop->pending && at != op; i++) { // bounded by the pending ops
        previous = at;
        at = at->internal.next;
    }
    assert(at == op);
    if (previous == nullptr)
        loop->head = op->internal.next;
    else
        previous->internal.next = op->internal.next;
    if (loop->tail == op) loop->tail = previous;
    loop->pending -= 1;
    op->internal.next = nullptr;
}

// Asks the kernel to tell us when the op can finish. False means it cannot be watched, so the op is simply run when
// the loop next looks (a regular file or /dev/null is always readable).
static bool smith_loop_watch(SmithLoop *loop, SmithOp *op) {
    struct kevent change = {};
    uintptr_t ident = 0;
    int16_t filter = 0;
    uint32_t flags = 0;
    intptr_t data = 0;
    switch (op->internal.kind) {
    case SmithOpKind_ConsoleRead:
    case SmithOpKind_PipeRead:
        ident = (uintptr_t)op->internal.descriptor;
        filter = EVFILT_READ;
        break;
    case SmithOpKind_ProcessWait:
        ident = (uintptr_t)op->internal.process;
        filter = EVFILT_PROC;
        flags = NOTE_EXIT;
        break;
    case SmithOpKind_Timeout:
        ident = (uintptr_t)op;
        filter = EVFILT_TIMER;
        flags = NOTE_NSECONDS;
        data = (intptr_t)op->internal.deadline;
        break;
    case SmithOpKind_None:
    case SmithOpKind_FileRead:
    case SmithOpKind_FileWrite:
        return false;
    }
    EV_SET(&change, ident, filter, EV_ADD | EV_ONESHOT, flags, data, op);
    return kevent(loop->queue, &change, 1, nullptr, 0, nullptr) == 0;
}

// Queues a freshly filled op. Ops that wait for the kernel are watched; the rest are ready when the loop runs.
static void smith_loop_submit(SmithLoop *loop, SmithOp *op) {
    assert(loop != nullptr && op->internal.kind != SmithOpKind_None);
    SmithFake *fake = loop->fake;
    SmithConsole *console = op->internal.console;
    bool buffered = console != nullptr && console->end > console->start && !op->internal.fill;
    op->internal.loop = loop;
    if (fake != nullptr) {
        op->internal.ready = op->internal.kind != SmithOpKind_Timeout; // fake timers wait for the clock
        if (op->internal.kind == SmithOpKind_Timeout) op->internal.deadline += fake->monotonic;
    } else if (buffered) {
        op->internal.ready = true;
    } else if (op->internal.kind == SmithOpKind_Timeout) {
        op->internal.ready = op->internal.deadline == 0 || !smith_loop_watch(loop, op);
    } else {
        op->internal.ready = !smith_loop_watch(loop, op);
    }
    smith_loop_link(loop, op);
}

static void smith_loop_finish_file(SmithLoop *loop, SmithOp *op) {
    SmithRead result = {};
    if (op->internal.kind == SmithOpKind_FileRead) {
        op->error = loop->fake != nullptr
                        ? smith_fake_read_file(loop->fake, op->internal.arena, op->internal.path, &result)
                        : smith_real_read_file(op->internal.arena, op->internal.path, &result);
        op->data = result.data;
        op->size = result.size;
    } else {
        op->error = loop->fake != nullptr
                        ? smith_fake_write_file(loop->fake, op->internal.path, op->internal.input, op->size)
                        : smith_real_write_file(op->internal.path, op->internal.input, op->size);
    }
}

// Serves a console read from what is buffered, else from the source; a fill goes to the console's own buffer.
static void smith_loop_finish_console(SmithLoop *loop, SmithOp *op) {
    SmithConsole *console = op->internal.console;
    size_t size = 0;
    if (!op->internal.fill && console->end > console->start) {
        size = console->end - console->start;
        if (size > op->internal.capacity) size = op->internal.capacity;
        memcpy(op->internal.destination, console->buffer + console->start, size);
        console->start += size;
    } else {
        int code = 0;
        ssize_t n = smith_console_source(loop, op->internal.destination, op->internal.capacity, &code);
        if (n < 0)
            op->error = smith_error_make(code, "read", "stdin");
        else
            size = (size_t)n;
        if (op->internal.fill) console->end += size;
    }
    op->size = size;
    op->end = size == 0 && op->error.code == 0 && op->internal.capacity > 0;
    if (!op->internal.fill && op->internal.destination != nullptr) op->internal.destination[size] = 0;
    op->data = (const char *)op->internal.destination;
}

// <sys/wait.h> writes its masks as octal literals, which C2y deprecates; the warning is about the SDK's macros.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-octal-literals"
static void smith_loop_finish_wait(SmithLoop *loop, SmithOp *op) {
    if (loop->fake != nullptr) {
        size_t index = (size_t)(-op->internal.process) - 1;
        op->exit_status = loop->fake->programs[index].status;
        return;
    }
    int status = 0;
    pid_t result = -1;
    for (size_t tries = 0; tries < 64 && result < 0; tries++) { // a signal may interrupt the wait
        result = waitpid(op->internal.process, &status, 0);
        if (result < 0 && errno != EINTR) break;
    }
    if (result < 0) {
        op->error = smith_error_make(errno, "wait", "process");
    } else if (WIFEXITED(status)) {
        op->exit_status = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        op->signal = WTERMSIG(status);
    }
}

#pragma clang diagnostic pop

// Runs the op's work, marks it done and takes it off the loop.
static void smith_loop_complete(SmithLoop *loop, SmithOp *op) {
    switch (op->internal.kind) {
    case SmithOpKind_FileRead:
    case SmithOpKind_FileWrite:
        smith_loop_finish_file(loop, op);
        break;
    case SmithOpKind_ConsoleRead:
        smith_loop_finish_console(loop, op);
        break;
    case SmithOpKind_PipeRead: {
        ssize_t n = read(op->internal.descriptor, op->internal.destination, op->internal.capacity);
        if (n < 0) op->error = smith_error_make(errno, "read", "pipe");
        op->size = n < 0 ? 0 : (size_t)n;
        op->end = n == 0;
        break;
    }
    case SmithOpKind_ProcessWait:
        smith_loop_finish_wait(loop, op);
        break;
    case SmithOpKind_Timeout:
    case SmithOpKind_None:
        break;
    }
    smith_loop_unlink(loop, op);
    op->done = true;
}

static bool smith_op_ready(const SmithLoop *loop, const SmithOp *op) {
    if (loop->fake != nullptr && op->internal.kind == SmithOpKind_Timeout)
        return loop->fake->monotonic >= op->internal.deadline;
    return op->internal.ready;
}

// Completes every op that needs no waiting, in submission order.
static size_t smith_loop_complete_ready(SmithLoop *loop) {
    size_t completed = 0;
    SmithOp *at = loop->head;
    size_t visit = loop->pending;
    for (size_t i = 0; i < visit && at != nullptr; i++) { // bounded by the ops pending now
        SmithOp *next = at->internal.next;
        if (smith_op_ready(loop, at)) {
            smith_loop_complete(loop, at);
            completed += 1;
        }
        at = next;
    }
    return completed;
}

static size_t smith_loop_run(SmithLoop *loop, int64_t timeout_ns) {
    assert(loop != nullptr);
    size_t completed = smith_loop_complete_ready(loop);
    if (loop->fake != nullptr || loop->pending == 0) return completed;
    if (completed > 0) timeout_ns = 0;
    struct timespec limit = {.tv_sec = timeout_ns / (int64_t)smith_io_nanoseconds,
                             .tv_nsec = timeout_ns % (int64_t)smith_io_nanoseconds};
    struct kevent events[smith_io_events_max];
    int count = kevent(loop->queue, nullptr, 0, events, (int)smith_io_events_max, timeout_ns < 0 ? nullptr : &limit);
    for (int i = 0; i < count; i++) { // at most smith_io_events_max
        SmithOp *op = events[i].udata;
        assert(op != nullptr && !op->done);
        smith_loop_complete(loop, op);
        completed += 1;
    }
    return completed;
}

// Runs the loop until the op is done: the blocking calls are this. The fake cannot wait for anything, so an op that is
// not ready there would wait forever and fails with EDEADLK instead.
static void smith_loop_wait(SmithLoop *loop, SmithOp *op) {
    constexpr size_t rounds_max = (size_t)1 << 24;
    for (size_t round = 0; !op->done && round < rounds_max; round++) {
        size_t completed = smith_loop_run(loop, smith_io_forever);
        if (completed == 0 && !op->done && loop->fake != nullptr) {
            smith_loop_unlink(loop, op);
            op->error = smith_error_make(EDEADLK, "wait", "in the test io, which nothing can advance");
            op->done = true;
        }
    }
    assert(op->done);
}

size_t smith_io_run(SmithIo *io, int64_t timeout_ns) {
    assert(io != nullptr);
    return smith_loop_run(io->loop, timeout_ns);
}

// ---------------------------------------------------------------- files

static void smith_op_start(SmithOp *op, SmithLoop *loop, SmithOpKind kind) {
    assert(op != nullptr && (op->done || op->internal.kind == SmithOpKind_None));
    *op = (SmithOp){};
    op->internal.kind = kind;
    op->internal.loop = loop;
}

void smith_files_read_async(SmithFiles *files, SmithOp *op, SmithArena *arena, const char *path) {
    assert(files != nullptr && arena != nullptr);
    smith_op_start(op, files->loop, SmithOpKind_FileRead);
    op->internal.arena = arena;
    op->internal.path = path;
    SmithError bad = smith_path_check(path);
    if (bad.code != 0) {
        op->error = bad;
        op->done = true;
        return;
    }
    smith_loop_submit(files->loop, op);
}

void smith_files_write_async(SmithFiles *files, SmithOp *op, const char *path, const void *data, size_t size) {
    assert(files != nullptr && (data != nullptr || size == 0));
    smith_op_start(op, files->loop, SmithOpKind_FileWrite);
    op->internal.path = path;
    op->internal.input = data;
    op->size = size;
    SmithError bad = smith_path_check(path);
    if (bad.code != 0) {
        op->error = bad;
        op->done = true;
        return;
    }
    smith_loop_submit(files->loop, op);
}

SmithRead smith_files_read(SmithFiles *files, SmithArena *arena, const char *path) {
    SmithOp op = {};
    smith_files_read_async(files, &op, arena, path);
    if (!op.done) smith_loop_wait(files->loop, &op);
    return (SmithRead){.error = op.error, .data = op.data, .size = op.size};
}

SmithError smith_files_write(SmithFiles *files, const char *path, const void *data, size_t size) {
    SmithOp op = {};
    smith_files_write_async(files, &op, path, data, size);
    if (!op.done) smith_loop_wait(files->loop, &op);
    return op.error;
}

// ---------------------------------------------------------------- console

static SmithError smith_console_write(SmithConsole *console, bool errors, const char *text) {
    assert(console != nullptr && text != nullptr);
    if (console->loop->fake != nullptr) return smith_fake_capture(console->loop->fake, errors, text);
    int code = smith_write_all(errors ? STDERR_FILENO : STDOUT_FILENO, text, strlen(text));
    return smith_error_make(code, "write", errors ? "stderr" : "stdout");
}

SmithError smith_console_print(SmithConsole *console, const char *text) {
    return smith_console_write(console, false, text);
}

SmithError smith_console_print_error(SmithConsole *console, const char *text) {
    return smith_console_write(console, true, text);
}

void smith_console_read_async(SmithConsole *console, SmithOp *op, SmithArena *arena, size_t max) {
    assert(console != nullptr && arena != nullptr && max > 0);
    smith_op_start(op, console->loop, SmithOpKind_ConsoleRead);
    op->internal.console = console;
    op->internal.arena = arena;
    op->internal.descriptor = STDIN_FILENO;
    op->internal.destination = smith_arena_push(arena, max + 1, 1);
    op->internal.capacity = max;
    smith_loop_submit(console->loop, op);
}

// Reads more input into the console's buffer, running the loop until it arrives. False at the end or on an error.
static bool smith_console_fill(SmithConsole *console, SmithError *error) {
    if (console->start > 0) {
        memmove(console->buffer, console->buffer + console->start, console->end - console->start);
        console->end -= console->start;
        console->start = 0;
    }
    SmithOp op = {};
    smith_op_start(&op, console->loop, SmithOpKind_ConsoleRead);
    op.internal.console = console;
    op.internal.fill = true;
    op.internal.descriptor = STDIN_FILENO;
    op.internal.destination = console->buffer + console->end;
    op.internal.capacity = sizeof console->buffer - console->end;
    smith_loop_submit(console->loop, &op);
    smith_loop_wait(console->loop, &op);
    *error = op.error;
    return op.error.code == 0 && op.size > 0;
}

SmithRead smith_console_read_line(SmithConsole *console, SmithArena *arena) {
    assert(console != nullptr && arena != nullptr);
    SmithGrow line = {.arena = arena};
    SmithRead result = {};
    bool newline = false;
    bool any = false;
    for (size_t rounds = 0; !newline && rounds <= arena->capacity; rounds++) { // each round consumes or ends
        const unsigned char *start = console->buffer + console->start;
        size_t available = console->end - console->start;
        const unsigned char *found = available > 0 ? memchr(start, '\n', available) : nullptr;
        size_t take = found != nullptr ? (size_t)(found - start) : available;
        if (available > 0) any = true;
        smith_grow_append(&line, start, take);
        console->start += take + (found != nullptr ? 1 : 0);
        newline = found != nullptr;
        if (!newline && !smith_console_fill(console, &result.error)) break;
    }
    if (result.error.code != 0) return result;
    result.end = !newline && !any;
    smith_grow_reserve(&line, 0);
    line.data[line.size] = 0;
    result.data = (const char *)line.data;
    result.size = line.size;
    return result;
}

// ---------------------------------------------------------------- clock

static uint64_t smith_time_ns(clockid_t clock) {
    struct timespec now = {};
    int result = clock_gettime(clock, &now);
    assert(result == 0);
    (void)result;
    return (uint64_t)now.tv_sec * smith_io_nanoseconds + (uint64_t)now.tv_nsec;
}

uint64_t smith_clock_now(const SmithClock *clock) {
    assert(clock != nullptr);
    return clock->loop->fake != nullptr ? clock->loop->fake->monotonic : smith_time_ns(CLOCK_MONOTONIC);
}

int64_t smith_clock_wall(const SmithClock *clock) {
    assert(clock != nullptr);
    return clock->loop->fake != nullptr ? clock->loop->fake->wall : (int64_t)smith_time_ns(CLOCK_REALTIME);
}

void smith_clock_advance(SmithClock *clock, uint64_t ns) {
    assert(clock != nullptr && clock->loop->fake != nullptr && "only the fake clock can be advanced");
    assert(ns <= (uint64_t)INT64_MAX);
    clock->loop->fake->monotonic += ns;
    clock->loop->fake->wall += (int64_t)ns;
}

void smith_clock_timeout_async(SmithClock *clock, SmithOp *op, uint64_t ns) {
    assert(clock != nullptr && ns <= (uint64_t)INT64_MAX);
    smith_op_start(op, clock->loop, SmithOpKind_Timeout);
    op->internal.deadline = ns; // relative; the fake adds its clock on submit, kqueue takes it relative
    smith_loop_submit(clock->loop, op);
}

SmithError smith_clock_sleep(SmithClock *clock, uint64_t ns) {
    SmithOp op = {};
    smith_clock_timeout_async(clock, &op, ns);
    smith_loop_wait(clock->loop, &op);
    return op.error;
}

// ---------------------------------------------------------------- processes

static const SmithFakeProgram *smith_fake_program(const SmithFake *fake, const char *name, int *id) {
    for (size_t i = 0; i < fake->program_count; i++) {
        if (strcmp(fake->programs[i].program, name) == 0) {
            *id = -(int)i - 1;
            return &fake->programs[i];
        }
    }
    return nullptr;
}

// Starts argv with stdin from /dev/null; out and err, when not -1, replace stdout and stderr in the child.
static SmithError smith_spawn(const char *const *argv, int out, int err, int *id) {
    posix_spawn_file_actions_t actions = {};
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (out >= 0) posix_spawn_file_actions_adddup2(&actions, out, STDOUT_FILENO);
    if (err >= 0) posix_spawn_file_actions_adddup2(&actions, err, STDERR_FILENO);
    pid_t pid = 0;
    // posix_spawnp takes char *const *, though it never writes to the arguments.
    int code = posix_spawnp(&pid, argv[0], &actions, nullptr, (char *const *)(uintptr_t)argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    *id = (int)pid;
    return smith_error_make(code, "run", argv[0]);
}

SmithError smith_processes_spawn(SmithProcesses *processes, const char *const *argv, SmithProcess *process) {
    assert(processes != nullptr && argv != nullptr && argv[0] != nullptr && process != nullptr);
    SmithFake *fake = processes->loop->fake;
    if (fake == nullptr) return smith_spawn(argv, -1, -1, &process->id);
    const SmithFakeProgram *program = smith_fake_program(fake, argv[0], &process->id);
    if (program == nullptr) return smith_error_make(ENOENT, "run", argv[0]);
    return smith_fake_capture(fake, false, program->output);
}

void smith_processes_wait_async(SmithProcesses *processes, SmithOp *op, SmithProcess process) {
    assert(processes != nullptr && process.id != 0);
    smith_op_start(op, processes->loop, SmithOpKind_ProcessWait);
    op->internal.process = process.id;
    smith_loop_submit(processes->loop, op);
}

// One captured stream of a running process: its pipe, its op, and what it has read.
typedef struct {
    int descriptor;
    bool open;
    SmithOp op;
    SmithGrow grow;
} SmithStream;

// Takes a finished read into the stream, closing it at the end; then submits the next read.
static void smith_stream_feed(SmithLoop *loop, SmithStream *stream) {
    if (!stream->open) return;
    if (stream->op.internal.kind != SmithOpKind_None) {
        if (!stream->op.done) return;
        stream->grow.size += stream->op.size;
        if (stream->op.end || stream->op.error.code != 0) {
            close(stream->descriptor);
            stream->open = false;
            stream->grow.data[stream->grow.size] = 0;
            return;
        }
    }
    smith_grow_reserve(&stream->grow, smith_io_chunk);
    smith_op_start(&stream->op, loop, SmithOpKind_PipeRead);
    stream->op.internal.descriptor = stream->descriptor;
    stream->op.internal.destination = stream->grow.data + stream->grow.size;
    stream->op.internal.capacity = stream->grow.capacity - stream->grow.size - 1;
    smith_loop_submit(loop, &stream->op);
}

static SmithError smith_run_pipes(const char *const *argv, SmithStream *out, SmithStream *err, int *id) {
    int pipes[2][2] = {{-1, -1}, {-1, -1}};
    if (pipe(pipes[0]) != 0 || pipe(pipes[1]) != 0) return smith_error_make(errno, "pipe for", argv[0]);
    for (size_t i = 0; i < 2; i++)
        for (size_t end = 0; end < 2; end++)
            fcntl(pipes[i][end], F_SETFD, FD_CLOEXEC);
    SmithError error = smith_spawn(argv, pipes[0][1], pipes[1][1], id);
    close(pipes[0][1]);
    close(pipes[1][1]);
    out->descriptor = pipes[0][0];
    err->descriptor = pipes[1][0];
    out->open = err->open = error.code == 0;
    if (error.code != 0) {
        close(pipes[0][0]);
        close(pipes[1][0]);
    }
    return error;
}

static SmithProcessResult smith_run_real(SmithLoop *loop, SmithArena *arena, const char *const *argv) {
    SmithProcessResult result = {};
    SmithStream out = {.grow = {.arena = arena}};
    SmithStream err = {.grow = {.arena = arena}};
    int id = 0;
    result.error = smith_run_pipes(argv, &out, &err, &id);
    if (result.error.code != 0) return result;
    SmithOp wait = {};
    smith_op_start(&wait, loop, SmithOpKind_ProcessWait);
    wait.internal.process = id;
    smith_loop_submit(loop, &wait);
    for (size_t rounds = 0; rounds <= arena->capacity + 64; rounds++) { // each round completes an op
        smith_stream_feed(loop, &out);
        smith_stream_feed(loop, &err);
        if (!out.open && !err.open && wait.done) break;
        smith_loop_run(loop, smith_io_forever);
    }
    assert(wait.done);
    result.error = wait.error;
    result.exit_status = wait.exit_status;
    result.signal = wait.signal;
    smith_grow_reserve(&out.grow, 0);
    smith_grow_reserve(&err.grow, 0);
    result.output = (const char *)out.grow.data;
    result.output_size = out.grow.size;
    result.errors = (const char *)err.grow.data;
    result.errors_size = err.grow.size;
    return result;
}

SmithProcessResult smith_processes_run(SmithProcesses *processes, SmithArena *arena, const char *const *argv) {
    assert(processes != nullptr && arena != nullptr && argv != nullptr && argv[0] != nullptr);
    SmithFake *fake = processes->loop->fake;
    if (fake == nullptr) return smith_run_real(processes->loop, arena, argv);
    SmithProcessResult result = {};
    int id = 0;
    const SmithFakeProgram *program = smith_fake_program(fake, argv[0], &id);
    if (program == nullptr) {
        result.error = smith_error_make(ENOENT, "run", argv[0]);
        return result;
    }
    result.exit_status = program->status;
    result.output_size = strlen(program->output);
    char *copy = smith_arena_push(arena, result.output_size + 1, 1);
    memcpy(copy, program->output, result.output_size);
    result.output = copy;
    result.errors = smith_arena_push(arena, 1, 1);
    return result;
}

// ---------------------------------------------------------------- arguments

const char *smith_args_get(const SmithArgs *args, size_t i) {
    assert(args != nullptr);
    return i < args->count ? args->values[i] : nullptr;
}

// ---------------------------------------------------------------- making io

static SmithLoop *smith_loop_make(SmithArena *arena, bool fake) {
    SmithLoop *loop = SMITH_ARENA_PUSH(arena, SmithLoop);
    loop->queue = -1;
    if (fake) {
        loop->fake = SMITH_ARENA_PUSH(arena, SmithFake);
        loop->fake->output = smith_arena_push(arena, smith_io_fake_capture, 1);
        loop->fake->errors = smith_arena_push(arena, smith_io_fake_capture, 1);
        size_t storage = (arena->capacity - arena->used) / 4 / smith_arena_align_max * smith_arena_align_max;
        loop->fake->storage = smith_arena_make(smith_arena_push(arena, storage, smith_arena_align_max), storage);
    } else {
        loop->queue = kqueue();
        if (loop->queue < 0) smith_io_stop("kqueue failed");
        fcntl(loop->queue, F_SETFD, FD_CLOEXEC);
    }
    return loop;
}

static void smith_io_link(SmithIo *io, size_t argc, const char *const *argv) {
    io->console.loop = io->loop;
    io->files.loop = io->loop;
    io->clock.loop = io->loop;
    io->processes.loop = io->loop;
    io->args.count = argc;
    io->args.values = argv;
}

// $SMITH_SEED replays a run; otherwise the seed is 64 bits of the system's entropy.
static uint64_t smith_io_seed(void) {
    const char *text = getenv("SMITH_SEED");
    if (text != nullptr) {
        char *end = nullptr;
        errno = 0;
        bool hex = text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
        unsigned long long value = strtoull(text, &end, hex ? 16 : 10);
        if (text[0] == '\0' || *end != '\0' || errno != 0) smith_io_stop("SMITH_SEED is not a number");
        return value;
    }
    uint64_t seed = 0;
    if (getentropy(&seed, sizeof seed) != 0) smith_io_stop("getentropy failed");
    return seed;
}

SmithIo smith_io_make(int argc, char **argv) {
    assert(argc >= 0);
    void *memory =
        mmap(nullptr, smith_io_reservation, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (memory == MAP_FAILED) smith_io_stop("cannot reserve memory for the arena");
    SmithIo io = {.arena = smith_arena_make(memory, smith_io_reservation)};
    io.loop = smith_loop_make(&io.arena, false);
    io.loop->memory = memory;
    io.loop->memory_size = smith_io_reservation;
    io.seed = smith_io_seed();
    io.key = smith_key_make(io.seed);
    smith_io_link(&io, (size_t)argc, (const char *const *)argv);
    return io;
}

void smith_io_close(SmithIo *io) {
    assert(io != nullptr);
    SmithLoop *loop = io->loop;
    if (loop->queue >= 0) close(loop->queue);
    if (loop->memory != nullptr) munmap(loop->memory, loop->memory_size);
    *io = (SmithIo){};
}

// ---------------------------------------------------------------- the test io

SmithIo smith_io_make_test(SmithIoTest test) {
    assert(test.memory != nullptr && test.capacity > 0);
    SmithIo io = {.arena = smith_arena_make(test.memory, test.capacity)};
    io.loop = smith_loop_make(&io.arena, true);
    io.seed = test.seed;
    io.key = smith_key_make(test.seed);
    SmithFake *fake = io.loop->fake;
    if (test.input != nullptr) {
        fake->input_size = strlen(test.input);
        char *copy = smith_arena_push(&io.arena, fake->input_size + 1, 1);
        memcpy(copy, test.input, fake->input_size);
        fake->input = copy;
    }
    const char **values = SMITH_ARENA_PUSH_ARRAY(&io.arena, const char *, test.argc + 1);
    for (size_t i = 0; i < test.argc; i++) {
        assert(test.argv != nullptr);
        values[i] = test.argv[i];
    }
    smith_io_link(&io, test.argc, values);
    return io;
}

SmithError smith_io_test_add_file(SmithIo *io, const char *path, const char *text) {
    SmithError bad = smith_path_check(path);
    if (bad.code != 0) return bad;
    return smith_fake_write_file(smith_fake_of(io->loop), path, text, strlen(text));
}

SmithError smith_io_test_add_program(SmithIo *io, const char *program, const char *output, int status) {
    SmithFake *fake = smith_fake_of(io->loop);
    if (fake->program_count == smith_io_fake_programs_max) return smith_error_make(ENOSPC, "script", program);
    SmithFakeProgram *entry = &fake->programs[fake->program_count++];
    char *name = smith_arena_push(&fake->storage, strlen(program) + 1, 1);
    memcpy(name, program, strlen(program));
    char *text = smith_arena_push(&fake->storage, strlen(output) + 1, 1);
    memcpy(text, output, strlen(output));
    *entry = (SmithFakeProgram){.program = name, .output = text, .status = status};
    return (SmithError){};
}

const char *smith_io_test_output(const SmithIo *io) {
    return smith_fake_of(io->loop)->output;
}

const char *smith_io_test_errors(const SmithIo *io) {
    return smith_fake_of(io->loop)->errors;
}
