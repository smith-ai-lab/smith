#include "../include/smith/io.h"

#include <Network/Network.h>
#include <assert.h>
#include <dirent.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdatomic.h>
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

constexpr size_t smith_io_reservation = (size_t)64 << 30;

constexpr size_t smith_io_fake_files_max = 64;
constexpr size_t smith_io_fake_programs_max = 16;
constexpr size_t smith_io_fake_capture = (size_t)64 << 10;
constexpr size_t smith_io_events_max = 16;
constexpr size_t smith_io_entries_max = (size_t)1 << 20; // entries of one folder
constexpr size_t smith_io_print_max = 4096;              // what smith_console_printf formats
constexpr size_t smith_io_chunk = 4096;
constexpr int smith_io_folder_mode = 0b111111111; // rwxrwxrwx, before the umask
constexpr int smith_io_file_mode = 0b110100100;   // rw-r--r--, before the umask
constexpr uint64_t smith_io_nanoseconds = 1'000'000'000;
constexpr size_t smith_io_fake_servers_max = 16;
constexpr size_t smith_io_request_max = 8192;
constexpr uintptr_t smith_io_wakeup = 1; // the ident of the loop's EVFILT_USER event
constexpr size_t smith_net_cas_tries = (size_t)1 << 20;

typedef struct {
    const char *path;
    unsigned char *data; // capacity bytes: the size bytes of the file and then zeros (a NUL at least)
    size_t size;
    size_t capacity;
    bool folder; // a folder made by smith_files_make_folder; the folders of a file's path need no entry
} SmithFakeFile;

typedef struct {
    const char *program;
    const char *output;
    int status;
} SmithFakeProgram;

// A server of the scripted network and the bytes its client has sent.
typedef struct {
    const char *host;
    uint16_t port;
    const unsigned char *response;
    size_t response_size;
    size_t chunk;
    int error;
    const char *tls_failure;
    bool used;
    char *request; // smith_io_request_max + 1 bytes
    size_t request_size;
} SmithFakeServer;

typedef struct {
    SmithFakeServer servers[smith_io_fake_servers_max];
    size_t count;
} SmithFakeNet;

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
    size_t cores;
} SmithFake;

// Network.framework's functions, looked up with dlopen when the first connection is made (see the net section).
typedef struct {
    typeof(nw_endpoint_create_host) *endpoint_create_host;
    typeof(nw_parameters_create_secure_tcp) *parameters_create_secure_tcp;
    typeof(nw_connection_create) *connection_create;
    typeof(nw_connection_set_queue) *set_queue;
    typeof(nw_connection_set_state_changed_handler) *set_state_handler;
    typeof(nw_connection_start) *start;
    typeof(nw_connection_cancel) *cancel;
    typeof(nw_connection_send) *send;
    typeof(nw_connection_receive) *receive;
    typeof(nw_error_get_error_domain) *error_domain;
    typeof(nw_error_get_error_code) *error_code;
    const nw_parameters_configure_protocol_block_t *configure_default;
    const nw_parameters_configure_protocol_block_t *configure_disable;
    const nw_content_context_t *context_message;
} SmithNetApi;

struct SmithLoop {
    int queue; // the kqueue, -1 in the fake
    SmithFake *fake;
    SmithOp *head;
    SmithOp *tail;
    size_t pending;
    void *memory; // the reservation, to release
    size_t memory_size;
    SmithFakeNet *servers;       // the scripted network, once a server is added; the fake io's only one
    _Atomic(SmithOp *) finished; // network ops whose callbacks have run: pushed by any thread, taken by the loop
    SmithNetApi net_api;         // Network.framework's functions, once the first connection needs them
    bool net_loaded;
    dispatch_queue_t net_queue; // the serial queue its callbacks run on
};

static void smith_net_drain(SmithLoop *loop, size_t *completed);
static void smith_net_complete_scripted(SmithLoop *loop, SmithOp *op);
static bool smith_net_scripted(const SmithLoop *loop);

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

// ---------------------------------------------------------------- descriptors

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
    if (file->folder) return smith_error_make(EISDIR, "read", path);
    unsigned char *copy = smith_arena_push(arena, file->size + 1, 1);
    if (file->size > 0) memcpy(copy, file->data, file->size);
    result->data = (const char *)copy;
    result->size = file->size;
    return (SmithError){};
}

// The entry for a path, made if it is not there. nullptr when the fake holds as many files as it can.
static SmithFakeFile *smith_fake_entry(SmithFake *fake, const char *path) {
    SmithFakeFile *file = smith_fake_find(fake, path);
    if (file != nullptr) return file;
    if (fake->file_count == smith_io_fake_files_max) return nullptr;
    file = &fake->files[fake->file_count++];
    *file = (SmithFakeFile){};
    char *name = smith_arena_push(&fake->storage, strlen(path) + 1, 1);
    memcpy(name, path, strlen(path));
    file->path = name;
    return file;
}

static SmithError smith_fake_write_file(SmithFake *fake, const char *path, const void *data, size_t size) {
    SmithFakeFile *file = smith_fake_entry(fake, path);
    if (file == nullptr) return smith_error_make(ENOSPC, "create", path);
    file->data = smith_arena_push(&fake->storage, size + 1, 1);
    if (size > 0) memcpy(file->data, data, size);
    file->size = size;
    file->capacity = size + 1;
    file->folder = false;
    return (SmithError){};
}

static SmithError smith_fake_append_file(SmithFake *fake, const char *path, const void *data, size_t size) {
    SmithFakeFile *file = smith_fake_entry(fake, path);
    if (file == nullptr) return smith_error_make(ENOSPC, "create", path);
    if (file->folder) return smith_error_make(EISDIR, "open", path);
    if (file->data == nullptr || file->size + size + 1 > file->capacity) { // grows by doubling
        size_t wanted = file->capacity * 2 > file->size + size + 1 ? file->capacity * 2 : file->size + size + 1;
        unsigned char *bigger = smith_arena_push(&fake->storage, wanted, 1);
        if (file->size > 0) memcpy(bigger, file->data, file->size);
        file->data = bigger;
        file->capacity = wanted;
    }
    if (size > 0) memcpy(file->data + file->size, data, size);
    file->size += size;
    file->data[file->size] = 0;
    return (SmithError){};
}

static SmithError smith_fake_write(SmithFake *fake, const char *path, const void *data, size_t size, SmithWrite mode) {
    switch (mode) {
    case SmithWrite_Replace:
    case SmithWrite_Atomic:
        return smith_fake_write_file(fake, path, data, size);
    case SmithWrite_Append:
        return smith_fake_append_file(fake, path, data, size);
    case SmithWrite_New:
        if (smith_fake_find(fake, path) != nullptr) return smith_error_make(EEXIST, "open", path);
        return smith_fake_write_file(fake, path, data, size);
    }
    return smith_error_make(EINVAL, "write", path);
}

// The length of a folder's path without trailing slashes; "." and "" are the fake's root, of length 0.
static size_t smith_fake_folder_length(const char *path) {
    size_t length = strlen(path);
    while (length > 1 && path[length - 1] == '/')
        length -= 1;
    if (length == 0 || (length == 1 && path[0] == '.')) return 0;
    return length;
}

// Whether a folder is at the path: made by smith_files_make_folder, or holding a file or folder of the fake.
static bool smith_fake_folder_exists(const SmithFake *fake, const char *path) {
    size_t length = smith_fake_folder_length(path);
    if (length == 0) return true;
    for (size_t i = 0; i < fake->file_count; i++) {
        const char *other = fake->files[i].path;
        size_t other_length = strlen(other);
        if (fake->files[i].folder && other_length == length && strncmp(other, path, length) == 0) return true;
        if (other_length > length && strncmp(other, path, length) == 0 && other[length] == '/') return true;
    }
    return false;
}

static SmithFileInfo smith_fake_info(const SmithFake *fake, const char *path) {
    SmithFileInfo info = {.writable = true};
    for (size_t i = 0; i < fake->file_count; i++) {
        if (strcmp(fake->files[i].path, path) == 0 && !fake->files[i].folder) {
            info.size = fake->files[i].size;
            return info;
        }
    }
    if (smith_fake_folder_exists(fake, path)) {
        info.folder = true;
        return info;
    }
    info.writable = false;
    info.error = smith_error_make(ENOENT, "stat", path);
    return info;
}

static SmithError smith_fake_make_folder(SmithFake *fake, const char *path) {
    SmithFileInfo info = smith_fake_info(fake, path);
    if (info.error.code == 0) return info.folder ? (SmithError){} : smith_error_make(EEXIST, "mkdir", path);
    SmithFakeFile *file = smith_fake_entry(fake, path);
    if (file == nullptr) return smith_error_make(ENOSPC, "mkdir", path);
    file->folder = true;
    return (SmithError){};
}

static SmithError smith_fake_remove(SmithFake *fake, const char *path) {
    for (size_t i = 0; i < fake->file_count; i++) {
        if (strcmp(fake->files[i].path, path) != 0) continue;
        if (fake->files[i].folder) return smith_error_make(EPERM, "remove", path);
        for (size_t j = i + 1; j < fake->file_count; j++)
            fake->files[j - 1] = fake->files[j];
        fake->file_count -= 1;
        return (SmithError){};
    }
    return smith_error_make(smith_fake_folder_exists(fake, path) ? EPERM : ENOENT, "remove", path);
}

static int smith_entry_compare(const void *a, const void *b) {
    return strcmp(((const SmithFileEntry *)a)->name, ((const SmithFileEntry *)b)->name);
}

// Adds what a path inside the listed folder shows there: its first name, a folder if the path goes on or is a folder.
static void smith_fake_list_add(SmithArena *arena, SmithFileList *list, SmithFileEntry *entries,
                                const SmithFakeFile *file, const char *rest) {
    const char *slash = strchr(rest, '/');
    size_t name_length = slash != nullptr ? (size_t)(slash - rest) : strlen(rest);
    if (name_length == 0) return;
    size_t at = 0;
    for (; at < list->count; at++) {
        if (strncmp(entries[at].name, rest, name_length) == 0 && entries[at].name[name_length] == 0) break;
    }
    if (at == list->count) {
        char *name = smith_arena_push(arena, name_length + 1, 1);
        memcpy(name, rest, name_length);
        entries[list->count++].name = name;
    }
    entries[at].folder = entries[at].folder || slash != nullptr || file->folder;
    entries[at].size = entries[at].folder ? 0 : file->size;
}

static SmithFileList smith_fake_list(SmithFake *fake, SmithArena *arena, const char *folder) {
    SmithFileList list = {};
    if (!smith_fake_folder_exists(fake, folder)) {
        list.error = smith_error_make(ENOENT, "open", folder);
        return list;
    }
    size_t length = smith_fake_folder_length(folder);
    SmithFileEntry *entries = SMITH_ARENA_PUSH_ARRAY(arena, SmithFileEntry, smith_io_fake_files_max + 1);
    for (size_t i = 0; i < fake->file_count; i++) {
        const SmithFakeFile *file = &fake->files[i];
        if (length > 0 && !(strncmp(file->path, folder, length) == 0 && file->path[length] == '/')) continue;
        smith_fake_list_add(arena, &list, entries, file, file->path + (length > 0 ? length + 1 : 0));
    }
    qsort(entries, list.count, sizeof *entries, smith_entry_compare);
    list.entries = entries;
    return list;
}

static SmithError smith_fake_map(SmithFake *fake, const char *path, SmithMapped *mapped) {
    SmithFakeFile *file = smith_fake_find(fake, path);
    if (file == nullptr || file->folder) return smith_error_make(ENOENT, "open", path);
    if (file->size == 0) return smith_error_make(EINVAL, "map", path);
    mapped->data = smith_arena_push(&fake->storage, file->size, smith_arena_align_max);
    memcpy(mapped->data, file->data, file->size);
    mapped->size = file->size;
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

// Opens path with the flags (writing, and what makes or keeps the file), writes all of data and closes.
static SmithError smith_real_put(const char *path, int flags, const void *data, size_t size) {
    int descriptor = open(path, flags, smith_io_file_mode);
    if (descriptor < 0) return smith_error_make(errno, "open", path);
    int code = smith_write_all(descriptor, data, size);
    if (close(descriptor) != 0 && code == 0) code = errno;
    return smith_error_make(code, "write", path);
}

// Writes "<path>.tmp" and renames it over the path, so the path holds the old file or the whole new one.
static SmithError smith_real_write_atomic(const char *path, const void *data, size_t size) {
    char temporary[PATH_MAX];
    int length = snprintf(temporary, sizeof temporary, "%s.tmp", path);
    if (length < 0 || (size_t)length >= sizeof temporary) return smith_error_make(ENAMETOOLONG, "write", path);
    SmithError error = smith_real_put(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, data, size);
    if (error.code == 0 && rename(temporary, path) != 0) error = smith_error_make(errno, "rename", path);
    if (error.code != 0) unlink(temporary);
    return error;
}

static SmithError smith_real_write_file(const char *path, const void *data, size_t size, SmithWrite mode) {
    switch (mode) {
    case SmithWrite_Replace:
        return smith_real_put(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, data, size);
    case SmithWrite_Atomic:
        return smith_real_write_atomic(path, data, size);
    case SmithWrite_Append:
        return smith_real_put(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, data, size);
    case SmithWrite_New:
        return smith_real_put(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, data, size);
    }
    return smith_error_make(EINVAL, "write", path);
}

// <sys/stat.h> writes S_ISDIR's masks as octal literals, which C2y deprecates; the warning is about the SDK's macros.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-octal-literals"

// mkdir -p: each folder of the path in turn; a folder that is there is fine, a file in the way is EEXIST.
static SmithError smith_real_make_folder(const char *path) {
    char partial[PATH_MAX];
    size_t length = strlen(path);
    if (length == 0 || length >= sizeof partial)
        return smith_error_make(length == 0 ? ENOENT : ENAMETOOLONG, "mkdir", path);
    memcpy(partial, path, length + 1);
    for (size_t i = 1; i <= length; i++) {
        if (partial[i] != '/' && partial[i] != 0) continue;
        char kept = partial[i];
        partial[i] = 0;
        struct stat info = {};
        if (mkdir(partial, smith_io_folder_mode) != 0) {
            int code = errno;
            if (code != EEXIST || stat(partial, &info) != 0 || !S_ISDIR(info.st_mode)) {
                return smith_error_make(code, "mkdir", path);
            }
        }
        partial[i] = kept;
    }
    return (SmithError){};
}

static SmithFileInfo smith_real_info(const char *path) {
    SmithFileInfo info = {};
    struct stat status = {};
    if (stat(path, &status) != 0) {
        info.error = smith_error_make(errno, "stat", path);
        return info;
    }
    info.folder = S_ISDIR(status.st_mode);
    info.size = info.folder ? 0 : (size_t)status.st_size;
    info.writable = access(path, W_OK) == 0;
    return info;
}

// Grows the entries array by doubling, the old block staying behind in the arena.
static SmithFileEntry *smith_real_entries_grow(SmithArena *arena, SmithFileEntry *entries, size_t count,
                                               size_t *capacity) {
    size_t wanted = *capacity == 0 ? 64 : *capacity * 2;
    SmithFileEntry *bigger = SMITH_ARENA_PUSH_ARRAY(arena, SmithFileEntry, wanted);
    if (count > 0) memcpy(bigger, entries, count * sizeof *entries);
    *capacity = wanted;
    return bigger;
}

static SmithFileList smith_real_list(SmithArena *arena, const char *folder) {
    SmithFileList list = {};
    DIR *directory = opendir(folder);
    if (directory == nullptr) {
        list.error = smith_error_make(errno, "open", folder);
        return list;
    }
    SmithFileEntry *entries = nullptr;
    size_t capacity = 0;
    for (size_t i = 0; i < smith_io_entries_max; i++) { // each round reads an entry or ends
        struct dirent *item = readdir(directory);
        if (item == nullptr) break;
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) continue;
        if (list.count == capacity) entries = smith_real_entries_grow(arena, entries, list.count, &capacity);
        struct stat status = {};
        bool known = fstatat(dirfd(directory), item->d_name, &status, 0) == 0;
        size_t length = strlen(item->d_name);
        char *name = smith_arena_push(arena, length + 1, 1);
        memcpy(name, item->d_name, length);
        entries[list.count++] = (SmithFileEntry){
            .name = name,
            .folder = known && S_ISDIR(status.st_mode),
            .size = known && !S_ISDIR(status.st_mode) ? (size_t)status.st_size : 0,
        };
    }
    closedir(directory);
    if (list.count > 1) qsort(entries, list.count, sizeof *entries, smith_entry_compare);
    list.entries = entries;
    return list;
}

static SmithMapped smith_real_map(const char *path) {
    SmithMapped mapped = {};
    int descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        mapped.error = smith_error_make(errno, "open", path);
        return mapped;
    }
    struct stat info = {};
    if (fstat(descriptor, &info) != 0) {
        mapped.error = smith_error_make(errno, "stat", path);
    } else if (info.st_size <= 0) {
        mapped.error = smith_error_make(EINVAL, "map", path);
    } else {
        void *bytes = mmap(nullptr, (size_t)info.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, descriptor, 0);
        if (bytes == MAP_FAILED) {
            mapped.error = smith_error_make(errno, "map", path);
        } else {
            mapped.data = bytes;
            mapped.size = (size_t)info.st_size;
        }
    }
    close(descriptor);
    return mapped;
}

#pragma clang diagnostic pop

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

// <sys/stat.h> writes S_ISREG's masks as octal literals, which C2y deprecates; the warning is about the SDK's macros.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-octal-literals"
// Whether kqueue can say when a read on the descriptor would finish: pipes, sockets and terminals, not files.
static bool smith_descriptor_waitable(int descriptor) {
    struct stat info = {};
    if (fstat(descriptor, &info) != 0) return false;
    return S_ISFIFO(info.st_mode) || S_ISSOCK(info.st_mode) || S_ISCHR(info.st_mode);
}
#pragma clang diagnostic pop

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
        // kqueue reports a regular file readable only while its offset is before the end, so at the end it would never
        // fire: a file (or anything but a pipe, a socket or a terminal) is read at once, and a read at its end returns
        // 0.
        if (!smith_descriptor_waitable(op->internal.descriptor)) return false;
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
    case SmithOpKind_NetConnect:
    case SmithOpKind_NetSend:
    case SmithOpKind_NetReceive:
    case SmithOpKind_NetClose:
        return !smith_net_scripted(loop); // a callback finishes it and wakes the loop; a script is ready at once
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
        op->error =
            loop->fake != nullptr
                ? smith_fake_write(loop->fake, op->internal.path, op->internal.input, op->size, op->internal.mode)
                : smith_real_write_file(op->internal.path, op->internal.input, op->size, op->internal.mode);
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
    case SmithOpKind_NetConnect:
    case SmithOpKind_NetSend:
    case SmithOpKind_NetReceive:
    case SmithOpKind_NetClose:
        if (smith_net_scripted(loop)) smith_net_complete_scripted(loop, op);
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
        if (events[i].filter == EVFILT_USER) {
            smith_net_drain(loop, &completed);
            continue;
        }
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
    assert(path != nullptr);
    op->internal.path = path;
    smith_loop_submit(files->loop, op);
}

void smith_files_write_async(SmithFiles *files, SmithOp *op, const char *path, const void *data, size_t size,
                             SmithWrite mode) {
    assert(files != nullptr && (data != nullptr || size == 0));
    smith_op_start(op, files->loop, SmithOpKind_FileWrite);
    assert(path != nullptr);
    op->internal.path = path;
    op->internal.mode = mode;
    op->internal.input = data;
    op->size = size;
    smith_loop_submit(files->loop, op);
}

SmithRead smith_files_read(SmithFiles *files, SmithArena *arena, const char *path) {
    SmithOp op = {};
    smith_files_read_async(files, &op, arena, path);
    if (!op.done) smith_loop_wait(files->loop, &op);
    return (SmithRead){.error = op.error, .data = op.data, .size = op.size};
}

SmithError smith_files_write(SmithFiles *files, const char *path, const void *data, size_t size, SmithWrite mode) {
    SmithOp op = {};
    smith_files_write_async(files, &op, path, data, size, mode);
    if (!op.done) smith_loop_wait(files->loop, &op);
    return op.error;
}

SmithError smith_files_remove(SmithFiles *files, const char *path) {
    assert(files != nullptr && path != nullptr);
    if (files->loop->fake != nullptr) return smith_fake_remove(files->loop->fake, path);
    return unlink(path) == 0 ? (SmithError){} : smith_error_make(errno, "remove", path);
}

SmithError smith_files_make_folder(SmithFiles *files, const char *path) {
    assert(files != nullptr && path != nullptr);
    if (files->loop->fake != nullptr) return smith_fake_make_folder(files->loop->fake, path);
    return smith_real_make_folder(path);
}

SmithFileInfo smith_files_info(SmithFiles *files, const char *path) {
    assert(files != nullptr && path != nullptr);
    if (files->loop->fake != nullptr) return smith_fake_info(files->loop->fake, path);
    return smith_real_info(path);
}

SmithFileList smith_files_list(SmithFiles *files, SmithArena *arena, const char *folder) {
    assert(files != nullptr && arena != nullptr && folder != nullptr);
    if (files->loop->fake != nullptr) return smith_fake_list(files->loop->fake, arena, folder);
    return smith_real_list(arena, folder);
}

SmithMapped smith_files_map(SmithFiles *files, const char *path) {
    assert(files != nullptr && path != nullptr);
    SmithMapped mapped = {};
    if (files->loop->fake != nullptr) {
        mapped.error = smith_fake_map(files->loop->fake, path, &mapped);
        return mapped;
    }
    return smith_real_map(path);
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

__attribute__((format(printf, 3, 0))) static SmithError smith_console_format(SmithConsole *console, bool errors,
                                                                             const char *format, va_list arguments) {
    char text[smith_io_print_max];
    int length = vsnprintf(text, sizeof text, format, arguments);
    if (length < 0) return smith_error_make(EINVAL, "format for", errors ? "stderr" : "stdout");
    if ((size_t)length >= sizeof text) return smith_error_make(EMSGSIZE, "print", errors ? "stderr" : "stdout");
    return smith_console_write(console, errors, text);
}

SmithError smith_console_printf(SmithConsole *console, const char *format, ...) {
    assert(console != nullptr && format != nullptr);
    va_list arguments;
    va_start(arguments, format);
    SmithError error = smith_console_format(console, false, format, arguments);
    va_end(arguments);
    return error;
}

SmithError smith_console_eprintf(SmithConsole *console, const char *format, ...) {
    assert(console != nullptr && format != nullptr);
    va_list arguments;
    va_start(arguments, format);
    SmithError error = smith_console_format(console, true, format, arguments);
    va_end(arguments);
    return error;
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

size_t smith_processes_cores(const SmithProcesses *processes) {
    assert(processes != nullptr);
    if (processes->loop->fake != nullptr) return processes->loop->fake->cores;
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    return cores > 0 ? (size_t)cores : 1;
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

// ---------------------------------------------------------------- net

// Network.framework is loaded with dlopen when the first connection is made, never linked: a program that makes no
// connection (smith routing a compile, say) pays nothing at start, where linking the framework costs about 2.5 ms in
// every process. The functions and the constants the C API names are looked up once and kept in the loop.

// Symbols are data or functions; either way dlsym returns an address. ISO C has no conversion from object pointers to
// function pointers, and dlsym's contract is exactly that conversion.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpedantic"
static bool smith_net_load(SmithNetApi *api) {
    void *library = dlopen("/System/Library/Frameworks/Network.framework/Network", RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) return false;
    struct {
        const char *name;
        void *address;
    } symbols[] = {
        {"nw_endpoint_create_host", nullptr},
        {"nw_parameters_create_secure_tcp", nullptr},
        {"nw_connection_create", nullptr},
        {"nw_connection_set_queue", nullptr},
        {"nw_connection_set_state_changed_handler", nullptr},
        {"nw_connection_start", nullptr},
        {"nw_connection_cancel", nullptr},
        {"nw_connection_send", nullptr},
        {"nw_connection_receive", nullptr},
        {"nw_error_get_error_domain", nullptr},
        {"nw_error_get_error_code", nullptr},
        {"_nw_parameters_configure_protocol_default_configuration", nullptr},
        {"_nw_parameters_configure_protocol_disable", nullptr},
        {"_nw_content_context_default_message", nullptr},
    };
    for (size_t i = 0; i < sizeof symbols / sizeof symbols[0]; i++) {
        symbols[i].address = dlsym(library, symbols[i].name);
        if (symbols[i].address == nullptr) return false;
    }
    api->endpoint_create_host = symbols[0].address;
    api->parameters_create_secure_tcp = symbols[1].address;
    api->connection_create = symbols[2].address;
    api->set_queue = symbols[3].address;
    api->set_state_handler = symbols[4].address;
    api->start = symbols[5].address;
    api->cancel = symbols[6].address;
    api->send = symbols[7].address;
    api->receive = symbols[8].address;
    api->error_domain = symbols[9].address;
    api->error_code = symbols[10].address;
    api->configure_default = (const nw_parameters_configure_protocol_block_t *)symbols[11].address;
    api->configure_disable = (const nw_parameters_configure_protocol_block_t *)symbols[12].address;
    api->context_message = (const nw_content_context_t *)symbols[13].address;
    return true;
}
#pragma clang diagnostic pop

// The functions, loaded on first use; nullptr if the framework is not there.
static const SmithNetApi *smith_net_api(SmithLoop *loop) {
    if (!loop->net_loaded) {
        if (!smith_net_load(&loop->net_api)) return nullptr;
        loop->net_queue = dispatch_queue_create("smith.net", DISPATCH_QUEUE_SERIAL);
        loop->net_loaded = loop->net_queue != nullptr;
    }
    return loop->net_loaded ? &loop->net_api : nullptr;
}

static bool smith_net_scripted(const SmithLoop *loop) {
    return loop->fake != nullptr || loop->servers != nullptr;
}

// --- finishing ops from any thread

// Wakes the loop's kevent. Safe from any thread.
static void smith_net_wake(SmithLoop *loop) {
    struct kevent wakeup = {};
    EV_SET(&wakeup, smith_io_wakeup, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
    int result = kevent(loop->queue, &wakeup, 1, nullptr, 0, nullptr);
    assert(result == 0);
    (void)result;
}

// Hands a finished op to the loop thread, which marks it done in smith_io_run. Callable from any thread, once per op.
static void smith_net_finish(SmithLoop *loop, SmithOp *op, SmithError error) {
    op->error = error;
    SmithOp *head = atomic_load(&loop->finished);
    for (size_t tries = 0; tries < smith_net_cas_tries; tries++) { // a lost race retries with the new head
        op->internal.finished = head;
        if (atomic_compare_exchange_weak(&loop->finished, &head, op)) break;
    }
    smith_net_wake(loop);
}

// The op waiting in a connection's slot, taken so that exactly one caller finishes it; nullptr if none is.
static SmithOp *smith_net_take(SmithConnection *connection, SmithOpKind kind) {
    size_t slot = (size_t)(kind - SmithOpKind_NetConnect);
    assert(slot < sizeof connection->internal.waiting / sizeof connection->internal.waiting[0]);
    return atomic_exchange(&connection->internal.waiting[slot], nullptr);
}

static void smith_net_finish_slot(SmithLoop *loop, SmithConnection *connection, SmithOpKind kind, SmithError error) {
    SmithOp *op = smith_net_take(connection, kind);
    if (op != nullptr) smith_net_finish(loop, op, error);
}

// Runs on the loop thread: completes every op the callbacks have finished. The list is last in, first out; it is
// turned around so that ops complete in the order they finished.
static void smith_net_drain(SmithLoop *loop, size_t *completed) {
    SmithOp *list = atomic_exchange(&loop->finished, nullptr);
    SmithOp *ordered = nullptr;
    for (size_t i = 0; list != nullptr && i < loop->pending; i++) { // bounded by the ops pending
        SmithOp *next = list->internal.finished;
        list->internal.finished = ordered;
        ordered = list;
        list = next;
    }
    for (size_t i = 0; ordered != nullptr && i <= loop->pending; i++) {
        SmithOp *next = ordered->internal.finished;
        smith_loop_complete(loop, ordered);
        *completed += 1;
        ordered = next;
    }
}

// --- errors

[[gnu::format(printf, 2, 3)]] static SmithError smith_error_text(int code, const char *format, ...) {
    SmithError error = {.code = code};
    va_list arguments;
    va_start(arguments, format);
    [[maybe_unused]] int written = vsnprintf(error.message, sizeof error.message, format, arguments);
    va_end(arguments);
    assert(written > 0);
    return error;
}

// What the system's TLS stack means by a status (Security/SecureTransport.h), for the ones a certificate check gives.
static const char *smith_net_tls_reason(int status) {
    switch (status) {
    case -9807:
        return "the certificate chain is not valid";
    case -9808:
        return "the certificate was rejected (wrong host, self-signed or untrusted)";
    case -9812:
        return "the certificate's root is not trusted";
    case -9813:
        return "the certificate has no trusted root";
    case -9814:
        return "the certificate has expired";
    case -9815:
        return "the certificate is not valid yet";
    case -9843:
        return "the certificate is not for this host";
    case -9806:
        return "the connection was closed during the handshake";
    default:
        return "the handshake failed";
    }
}

// A Network.framework error as a SmithError, with the connection's host in the message.
static SmithError smith_net_error(const SmithNetApi *api, nw_error_t error, const char *name) {
    int code = api->error_code(error);
    switch (api->error_domain(error)) {
    case nw_error_domain_posix:
        return smith_error_make(code, "connect", name);
    case nw_error_domain_dns:
        return smith_error_text(ENOENT, "connect %s: cannot find the host (DNS error %d)", name, code);
    case nw_error_domain_tls:
        return smith_error_text(EAUTH, "connect %s: TLS: %s (status %d)", name, smith_net_tls_reason(code), code);
    default:
        return smith_error_text(EIO, "connect %s: network error %d", name, code);
    }
}

static SmithError smith_net_closed(const char *name) {
    return smith_error_make(ECANCELED, "use closed connection", name);
}

// --- real connections

// The state callback, on the dispatch queue: finishes the connect when the connection is ready or fails, fails what is
// pending when it fails, and on the last state (cancelled) finishes the rest and the close.
static void smith_net_on_state(SmithLoop *loop, SmithConnection *connection, nw_connection_state_t state,
                               nw_error_t error) {
    const SmithNetApi *api = &loop->net_api;
    nw_connection_t handle = connection->internal.handle;
    if (state == nw_connection_state_ready) {
        smith_net_finish_slot(loop, connection, SmithOpKind_NetConnect, (SmithError){});
    } else if (error != nullptr && (state == nw_connection_state_failed || state == nw_connection_state_waiting)) {
        SmithError failure = smith_net_error(api, error, connection->internal.name);
        smith_net_finish_slot(loop, connection, SmithOpKind_NetConnect, failure);
        smith_net_finish_slot(loop, connection, SmithOpKind_NetSend, failure);
        smith_net_finish_slot(loop, connection, SmithOpKind_NetReceive, failure);
        api->cancel(handle); // the cancelled state follows, and the program closes the connection
    } else if (state == nw_connection_state_cancelled) {
        atomic_store(&connection->internal.closed, true);
        SmithError gone = smith_net_closed(connection->internal.name);
        smith_net_finish_slot(loop, connection, SmithOpKind_NetConnect, gone);
        smith_net_finish_slot(loop, connection, SmithOpKind_NetSend, gone);
        smith_net_finish_slot(loop, connection, SmithOpKind_NetReceive, gone);
        smith_net_finish_slot(loop, connection, SmithOpKind_NetClose, (SmithError){});
        os_release(handle);
    }
}

static void smith_net_begin(SmithLoop *loop, SmithOp *op, SmithConnection *connection, const char *host, uint16_t port,
                            bool tls) {
    const SmithNetApi *api = smith_net_api(loop);
    if (api == nullptr) {
        atomic_store(&connection->internal.waiting[0], op);
        smith_net_finish_slot(loop, connection, SmithOpKind_NetConnect,
                              smith_error_make(ENOSYS, "connect", "(Network.framework is not available)"));
        return;
    }
    char service[8];
    snprintf(service, sizeof service, "%u", (unsigned)port);
    nw_endpoint_t endpoint = api->endpoint_create_host(host, service);
    nw_parameters_t parameters = api->parameters_create_secure_tcp(
        tls ? *api->configure_default : *api->configure_disable, *api->configure_default);
    nw_connection_t handle = api->connection_create(endpoint, parameters);
    os_release(endpoint);
    os_release(parameters);
    connection->internal.handle = handle;
    atomic_store(&connection->internal.waiting[0], op);
    api->set_queue(handle, loop->net_queue);
    api->set_state_handler(handle, ^(nw_connection_state_t state, nw_error_t error) {
        smith_net_on_state(loop, connection, state, error);
    });
    api->start(handle);
}

// Copies a received block into the buffer, up to its capacity.
static size_t smith_net_copy(dispatch_data_t content, unsigned char *buffer, size_t capacity) {
    __block size_t copied = 0;
    if (content == nullptr) return 0;
    dispatch_data_apply(content, ^bool(dispatch_data_t region, size_t offset, const void *bytes, size_t size) {
        (void)region;
        (void)offset;
        size_t take = size < capacity - copied ? size : capacity - copied;
        memcpy(buffer + copied, bytes, take);
        copied += take;
        return true;
    });
    return copied;
}

static void smith_net_received(SmithLoop *loop, SmithConnection *connection, dispatch_data_t content, bool complete,
                               nw_error_t error) {
    SmithOp *op = smith_net_take(connection, SmithOpKind_NetReceive);
    if (op == nullptr) return;
    op->size = smith_net_copy(content, op->internal.destination, op->internal.capacity);
    op->data = (const char *)op->internal.destination;
    SmithError failure = {};
    if (error != nullptr) failure = smith_net_error(&loop->net_api, error, connection->internal.name);
    if (complete && error == nullptr) connection->internal.end = true;
    op->end = connection->internal.end && op->size == 0;
    smith_net_finish(loop, op, failure);
}

static void smith_net_receive_real(SmithLoop *loop, SmithOp *op, SmithConnection *connection) {
    const SmithNetApi *api = &loop->net_api;
    if (connection->internal.end) { // the peer finished with the last block
        op->data = (const char *)op->internal.destination;
        op->end = true;
        smith_net_finish(loop, op, (SmithError){});
        return;
    }
    atomic_store(&connection->internal.waiting[SmithOpKind_NetReceive - SmithOpKind_NetConnect], op);
    api->receive(connection->internal.handle, 1, (uint32_t)op->internal.capacity,
                 ^(dispatch_data_t content, nw_content_context_t context, bool complete, nw_error_t error) {
                     (void)context;
                     smith_net_received(loop, connection, content, complete, error);
                 });
}

static void smith_net_send_real(SmithLoop *loop, SmithOp *op, SmithConnection *connection) {
    const SmithNetApi *api = &loop->net_api;
    atomic_store(&connection->internal.waiting[SmithOpKind_NetSend - SmithOpKind_NetConnect], op);
    dispatch_data_t data =
        dispatch_data_create(op->internal.input, op->size, loop->net_queue, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    api->send(connection->internal.handle, data, *api->context_message, true, ^(nw_error_t error) {
        SmithError failure = {};
        if (error != nullptr) failure = smith_net_error(&loop->net_api, error, connection->internal.name);
        smith_net_finish_slot(loop, connection, SmithOpKind_NetSend, failure);
    });
    dispatch_release(data);
}

// --- scripted connections

static SmithFakeServer *smith_net_server_find(SmithFakeNet *net, const char *host, uint16_t port, size_t n,
                                              bool only_unused) {
    for (size_t i = 0; i < net->count; i++) {
        SmithFakeServer *server = &net->servers[i];
        if (server->port != port || strcmp(server->host, host) != 0) continue;
        if (only_unused ? !server->used : n-- == 0) return server;
    }
    return nullptr;
}

static void smith_net_scripted_connect(SmithLoop *loop, SmithOp *op, SmithConnection *connection) {
    const char *host = connection->internal.name;
    SmithFakeServer *server = nullptr;
    if (loop->servers != nullptr)
        server = smith_net_server_find(loop->servers, host, (uint16_t)op->internal.deadline, 0, true);
    if (server == nullptr) {
        op->error = smith_error_make(ECONNREFUSED, "connect", host);
    } else if (server->tls_failure != nullptr) {
        server->used = true;
        op->error = smith_error_text(EAUTH, "connect %s: TLS: %s", host, server->tls_failure);
    } else {
        server->used = true;
        connection->internal.script = server;
        connection->internal.position = 0;
    }
}

static void smith_net_scripted_receive(SmithOp *op, SmithConnection *connection) {
    SmithFakeServer *server = connection->internal.script;
    size_t left = server->response_size - connection->internal.position;
    size_t take = left < op->internal.capacity ? left : op->internal.capacity;
    if (server->chunk > 0 && take > server->chunk) take = server->chunk;
    if (take > 0) memcpy(op->internal.destination, server->response + connection->internal.position, take);
    connection->internal.position += take;
    op->data = (const char *)op->internal.destination;
    op->size = take;
    if (take > 0) return;
    if (server->error != 0)
        op->error = smith_error_make(server->error, "read", connection->internal.name);
    else
        op->end = true;
}

static void smith_net_scripted_send(SmithOp *op, SmithConnection *connection) {
    SmithFakeServer *server = connection->internal.script;
    if (server->request_size + op->size > smith_io_request_max) {
        op->error = smith_error_make(ENOSPC, "send", connection->internal.name);
        return;
    }
    memcpy(server->request + server->request_size, op->internal.input, op->size);
    server->request_size += op->size;
    server->request[server->request_size] = 0;
}

static void smith_net_complete_scripted(SmithLoop *loop, SmithOp *op) {
    SmithConnection *connection = op->internal.connection;
    if (op->internal.kind == SmithOpKind_NetConnect) {
        smith_net_scripted_connect(loop, op, connection);
        return;
    }
    if (op->internal.kind == SmithOpKind_NetClose) {
        atomic_store(&connection->internal.closed, true);
    } else if (connection->internal.script == nullptr || atomic_load(&connection->internal.closed)) {
        op->error = smith_net_closed(connection->internal.name);
    } else if (op->internal.kind == SmithOpKind_NetSend) {
        smith_net_scripted_send(op, connection);
    } else {
        smith_net_scripted_receive(op, connection);
    }
}

// --- the calls

static void smith_net_start(SmithNet *net, SmithOp *op, SmithConnection *connection, SmithOpKind kind) {
    assert(net != nullptr && connection != nullptr);
    smith_op_start(op, net->loop, kind);
    op->internal.connection = connection;
}

void smith_net_connect_async(SmithNet *net, SmithOp *op, SmithConnection *connection, const char *host, uint16_t port,
                             bool tls) {
    assert(host != nullptr && host[0] != 0);
    smith_net_start(net, op, connection, SmithOpKind_NetConnect);
    *connection = (SmithConnection){};
    connection->internal.loop = net->loop;
    snprintf(connection->internal.name, sizeof connection->internal.name, "%s", host);
    op->internal.deadline = port; // the scripted network reads the port from here
    smith_loop_submit(net->loop, op);
    if (!smith_net_scripted(net->loop)) smith_net_begin(net->loop, op, connection, host, port, tls);
}

void smith_net_send_async(SmithNet *net, SmithOp *op, SmithConnection *connection, const void *data, size_t size) {
    assert(data != nullptr || size == 0);
    smith_net_start(net, op, connection, SmithOpKind_NetSend);
    op->internal.input = data;
    op->size = size;
    smith_loop_submit(net->loop, op);
    if (smith_net_scripted(net->loop)) return;
    if (atomic_load(&connection->internal.closed) || size == 0) {
        smith_net_finish(net->loop, op, size == 0 ? (SmithError){} : smith_net_closed(connection->internal.name));
        return;
    }
    smith_net_send_real(net->loop, op, connection);
}

void smith_net_receive_async(SmithNet *net, SmithOp *op, SmithConnection *connection, void *buffer, size_t capacity) {
    assert(buffer != nullptr && capacity > 0 && capacity <= UINT32_MAX);
    smith_net_start(net, op, connection, SmithOpKind_NetReceive);
    op->internal.destination = buffer;
    op->internal.capacity = capacity;
    smith_loop_submit(net->loop, op);
    if (smith_net_scripted(net->loop)) return;
    if (atomic_load(&connection->internal.closed)) {
        smith_net_finish(net->loop, op, smith_net_closed(connection->internal.name));
        return;
    }
    smith_net_receive_real(net->loop, op, connection);
}

void smith_net_close_async(SmithNet *net, SmithOp *op, SmithConnection *connection) {
    smith_net_start(net, op, connection, SmithOpKind_NetClose);
    smith_loop_submit(net->loop, op);
    if (smith_net_scripted(net->loop)) return;
    if (connection->internal.handle == nullptr || atomic_load(&connection->internal.closed)) {
        smith_net_finish(net->loop, op, (SmithError){});
        return;
    }
    atomic_store(&connection->internal.waiting[SmithOpKind_NetClose - SmithOpKind_NetConnect], op);
    const SmithNetApi *api = &net->loop->net_api;
    api->cancel(connection->internal.handle);
    if (atomic_load(&connection->internal.closed))
        smith_net_finish_slot(net->loop, connection, SmithOpKind_NetClose, (SmithError){});
}

SmithError smith_net_connect(SmithNet *net, SmithConnection *connection, const char *host, uint16_t port, bool tls) {
    SmithOp op = {};
    smith_net_connect_async(net, &op, connection, host, port, tls);
    smith_loop_wait(net->loop, &op);
    return op.error;
}

SmithError smith_net_send(SmithNet *net, SmithConnection *connection, const void *data, size_t size) {
    SmithOp op = {};
    smith_net_send_async(net, &op, connection, data, size);
    smith_loop_wait(net->loop, &op);
    return op.error;
}

SmithRead smith_net_receive(SmithNet *net, SmithConnection *connection, void *buffer, size_t capacity) {
    SmithOp op = {};
    smith_net_receive_async(net, &op, connection, buffer, capacity);
    smith_loop_wait(net->loop, &op);
    return (SmithRead){.error = op.error, .data = op.data, .size = op.size, .end = op.end};
}

SmithError smith_net_close(SmithNet *net, SmithConnection *connection) {
    SmithOp op = {};
    smith_net_close_async(net, &op, connection);
    smith_loop_wait(net->loop, &op);
    return op.error;
}

// --- the scripted network's setup

SmithError smith_io_test_add_server(SmithIo *io, SmithNetServer server) {
    assert(io != nullptr && server.host != nullptr && (server.response != nullptr || server.response_size == 0));
    SmithLoop *loop = io->loop;
    if (loop->servers == nullptr) loop->servers = SMITH_ARENA_PUSH(&io->arena, SmithFakeNet);
    SmithFakeNet *net = loop->servers;
    if (net->count == smith_io_fake_servers_max) return smith_error_make(ENOSPC, "script", server.host);
    SmithFakeServer *entry = &net->servers[net->count++];
    char *host = smith_arena_push(&io->arena, strlen(server.host) + 1, 1);
    memcpy(host, server.host, strlen(server.host));
    unsigned char *response = smith_arena_push(&io->arena, server.response_size + 1, 1);
    if (server.response_size > 0) memcpy(response, server.response, server.response_size);
    char *tls_failure = nullptr;
    if (server.tls_failure != nullptr) {
        tls_failure = smith_arena_push(&io->arena, strlen(server.tls_failure) + 1, 1);
        memcpy(tls_failure, server.tls_failure, strlen(server.tls_failure));
    }
    *entry = (SmithFakeServer){.host = host,
                               .port = server.port,
                               .response = response,
                               .response_size = server.response_size,
                               .chunk = server.chunk,
                               .error = server.error,
                               .tls_failure = tls_failure,
                               .request = smith_arena_push(&io->arena, smith_io_request_max + 1, 1)};
    return (SmithError){};
}

const char *smith_io_test_request(const SmithIo *io, const char *host, uint16_t port, size_t n) {
    assert(io != nullptr && host != nullptr);
    if (io->loop->servers == nullptr) return nullptr;
    const SmithFakeServer *server = smith_net_server_find(io->loop->servers, host, port, n, false);
    return server != nullptr && server->used ? server->request : nullptr;
}

// ---------------------------------------------------------------- arguments

const char *smith_args_get(const SmithArgs *args, size_t i) {
    assert(args != nullptr);
    return i < args->count ? args->values[i] : nullptr;
}

const char *smith_args_env(const SmithArgs *args, const char *name) {
    assert(args != nullptr && name != nullptr && name[0] != 0);
    size_t length = strlen(name);
    if (args->environment == nullptr) return getenv(name); // the real io asks the process, which setenv may change
    for (size_t i = 0; i < smith_io_entries_max && args->environment[i] != nullptr; i++) {
        const char *entry = args->environment[i];
        if (strncmp(entry, name, length) == 0 && entry[length] == '=') return entry + length + 1;
    }
    return nullptr;
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
        struct kevent wakeup = {};
        EV_SET(&wakeup, smith_io_wakeup, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
        if (kevent(loop->queue, &wakeup, 1, nullptr, 0, nullptr) != 0) smith_io_stop("kqueue failed");
    }
    return loop;
}

static void smith_io_link(SmithIo *io, size_t argc, const char *const *argv, const char *const *environment) {
    io->console.loop = io->loop;
    io->files.loop = io->loop;
    io->clock.loop = io->loop;
    io->processes.loop = io->loop;
    io->net.loop = io->loop;
    io->args.count = argc;
    io->args.values = argv;
    io->args.environment = environment;
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
    smith_io_link(&io, (size_t)argc, (const char *const *)argv, nullptr);
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
    size_t entries = 0;
    for (; test.environment != nullptr && test.environment[entries] != nullptr; entries++) {
    }
    const char **variables = SMITH_ARENA_PUSH_ARRAY(&io.arena, const char *, entries + 1);
    for (size_t i = 0; i < entries; i++)
        variables[i] = test.environment[i];
    fake->cores = test.cores > 0 ? test.cores : 1;
    smith_io_link(&io, test.argc, values, variables);
    return io;
}

SmithError smith_io_test_add_file(SmithIo *io, const char *path, const char *text) {
    assert(io != nullptr && path != nullptr && text != nullptr);
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
