// What the language server's layer (runtime/lsp*.smith) needs and no .smith module offers yet: threads and locks, pipes
// and descriptors, the process's place, files, the clock and the environment. Every function is a few lines over libc
// and carries no decision; the header names the issue that closes each gap, and the function goes with it.
#include "lsp_edge.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// ---- Threads and locks (issue #556)

typedef struct {
    SmithLspBody body;
    void *context;
} SmithLspStart;

// The arguments of the threads being started, one for each thread the layer ever starts (the editor's reader, the
// server's reader, the background compile, the query client): a thread copies its slot before it returns from start.
static SmithLspStart smith_lsp_starts[8];
static unsigned smith_lsp_started;
static pthread_mutex_t smith_lsp_start_lock = PTHREAD_MUTEX_INITIALIZER;

static void *smith_lsp_thread_main(void *argument) {
    SmithLspStart start = *(SmithLspStart *)argument;
    start.body(start.context);
    return nullptr;
}

bool smith_lsp_thread_start(uint64_t *thread, SmithLspBody body, void *context) {
    pthread_mutex_lock(&smith_lsp_start_lock);
    SmithLspStart *slot = &smith_lsp_starts[smith_lsp_started % (sizeof smith_lsp_starts / sizeof smith_lsp_starts[0])];
    smith_lsp_started += 1;
    *slot = (SmithLspStart){.body = body, .context = context};
    pthread_t id;
    bool started = pthread_create(&id, nullptr, smith_lsp_thread_main, slot) == 0;
    pthread_mutex_unlock(&smith_lsp_start_lock);
    if (started) *thread = (uint64_t)(uintptr_t)id;
    return started;
}

void smith_lsp_thread_join(uint64_t thread) {
    pthread_join((pthread_t)(uintptr_t)thread, nullptr);
}

void smith_lsp_thread_detach(uint64_t thread) {
    pthread_detach((pthread_t)(uintptr_t)thread);
}

static pthread_mutex_t smith_lsp_locks[SmithLspLock_Count] = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
                                                              PTHREAD_MUTEX_INITIALIZER};

void smith_lsp_lock(SmithLspLock which) {
    pthread_mutex_lock(&smith_lsp_locks[which]);
}

void smith_lsp_unlock(SmithLspLock which) {
    pthread_mutex_unlock(&smith_lsp_locks[which]);
}

// ---- Pipes and descriptors (issue #557)

bool smith_lsp_pipe(int *read_end, int *write_end) {
    int ends[2];
    if (pipe(ends) != 0) return false;
    *read_end = ends[0];
    *write_end = ends[1];
    return true;
}

int smith_lsp_duplicate(int descriptor) {
    return dup(descriptor);
}

bool smith_lsp_redirect(int from, int onto) {
    if (dup2(from, onto) < 0) return false;
    close(from);
    return true;
}

bool smith_lsp_wait_readable(int descriptor, int milliseconds) {
    struct pollfd watch = {.fd = descriptor, .events = POLLIN, .revents = 0};
    return poll(&watch, 1, milliseconds) > 0;
}

// ---- The process's place (issue #558)

bool smith_lsp_working_folder(char *out, size_t size) {
    return getcwd(out, size) != nullptr;
}

bool smith_lsp_real_path(const char *path, char *out, size_t size) {
    char resolved[PATH_MAX];
    if (realpath(path, resolved) == nullptr) return false;
    size_t length = 0;
    while (resolved[length] != '\0')
        length++;
    if (length + 1 > size) return false;
    for (size_t i = 0; i <= length; i++)
        out[i] = resolved[i];
    return true;
}

bool smith_lsp_modified_at_or_after(const char *path, const char *than) {
    struct stat first = {};
    struct stat second = {};
    return stat(path, &first) == 0 && stat(than, &second) == 0 && first.st_mtime >= second.st_mtime;
}

int smith_lsp_process_id(void) {
    return (int)getpid();
}

// ---- Files, descriptors, the clock and the environment (issue #564)

int64_t smith_lsp_file_size(const char *path) {
    struct stat info = {};
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) return -1;
    return (int64_t)info.st_size;
}

bool smith_lsp_file_read(const char *path, uint8_t *buffer, size_t size) {
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return false;
    size_t done = 0;
    for (size_t round = 0; done < size && round <= size; round++) { // each round reads a byte or ends
        ssize_t got = read(descriptor, buffer + done, size - done);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        done += (size_t)got;
    }
    close(descriptor);
    return done == size;
}

bool smith_lsp_exists(const char *path) {
    return access(path, F_OK) == 0;
}

ptrdiff_t smith_lsp_read(int descriptor, uint8_t *buffer, size_t size) {
    for (int tries = 0; tries < 4096; tries++) { // a signal may interrupt the read
        ssize_t got = read(descriptor, buffer, size);
        if (got >= 0) return (ptrdiff_t)got;
        if (errno != EINTR) return -1;
    }
    return -1;
}

bool smith_lsp_write_all(int descriptor, const uint8_t *data, size_t size) {
    size_t done = 0;
    for (size_t round = 0; done < size && round <= size + 4096; round++) { // each round writes a byte or ends
        ssize_t put = write(descriptor, data + done, size - done);
        if (put < 0 && errno == EINTR) continue;
        if (put <= 0) return false;
        done += (size_t)put;
    }
    return done == size;
}

void smith_lsp_close(int descriptor) {
    close(descriptor);
}

uint64_t smith_lsp_now(void) {
    struct timespec time = {};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1'000'000'000U + (uint64_t)time.tv_nsec;
}

const char *smith_lsp_environment(const char *name) {
    return getenv(name);
}

// What the layer's threads share and nothing frees: the requests waiting for an answer, the open documents, the
// readers.
static alignas(64) unsigned char smith_lsp_block[4 << 20];

void *smith_lsp_memory(size_t *size) {
    *size = sizeof smith_lsp_block;
    return smith_lsp_block;
}
