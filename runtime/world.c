// What the executor, `smith mcp` and the verbs do to the world besides running processes: read, write, remove and
// make files, ask about a path, read the clock and reserve memory. Plain calls into libc (open, read, write, rename,
// unlink, mkdir, stat, clock_gettime, mmap), so a handler table interposes them as it does for any C code: there is no
// value to pass and nothing here to swap (owner, 2026-11-06, #2657: "we want io FULLY gone"). Errors are values: an
// errno code and a message, as `open config.txt: No such file or directory`.
//
// The read puts the bytes in the arena it is given, with a zero after them that the size does not count, so a caller
// can hand the text to C.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <smith/arena.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

constexpr size_t smith_world_reservation = (size_t)64 << 30;
constexpr uint64_t smith_world_second = 1'000'000'000;
constexpr mode_t smith_world_file_mode = 0666; // the umask takes its share
constexpr mode_t smith_world_folder_mode = 0777;

typedef struct {
    int code; // 0 for success, otherwise an errno value
    char message[192];
} SmithWorldError;

typedef struct {
    SmithWorldError error;
    const uint8_t *data; // size bytes and a zero; null when the read failed
    size_t size;
} SmithWorldRead;

typedef struct {
    SmithWorldError error; // ENOENT if there is nothing at the path
    bool folder;
    size_t size; // bytes of a file; 0 for a folder
} SmithWorldInfo;

typedef enum {
    SmithWorld_Replace, // creates the file or replaces what it held
    SmithWorld_Atomic,  // the bytes go to "<path>.tmp" and are renamed over the path
    SmithWorld_Append   // adds to the end, creating the file if it is not there
} SmithWorldWrite;

static SmithWorldError smith_world_error(int code, const char *what, const char *path) {
    SmithWorldError error = {.code = code};
    if (code != 0) snprintf(error.message, sizeof error.message, "%s %s: %s", what, path, strerror(code));
    return error;
}

// A whole file in the arena. A name that is not a file is the error of its open or read.
static SmithWorldRead smith_world_read(SmithArena *arena, const char *path) {
    int descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) return (SmithWorldRead){.error = smith_world_error(errno, "open", path)};
    struct stat status;
    if (fstat(descriptor, &status) != 0 || S_ISDIR(status.st_mode)) {
        int code = S_ISDIR(status.st_mode) ? EISDIR : errno;
        close(descriptor);
        return (SmithWorldRead){.error = smith_world_error(code, "read", path)};
    }
    size_t capacity = status.st_size > 0 ? (size_t)status.st_size : 0;
    uint8_t *data = smith_arena_push(arena, capacity + 1, 1);
    size_t used = 0;
    int code = 0;
    while (used < capacity) { // bounded by the size the file had
        ssize_t count = read(descriptor, data + used, capacity - used);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) code = errno;
        if (count <= 0) break;
        used += (size_t)count;
    }
    close(descriptor);
    data[used] = 0;
    if (code != 0) return (SmithWorldRead){.error = smith_world_error(code, "read", path)};
    return (SmithWorldRead){.data = data, .size = used};
}

static SmithWorldError smith_world_write_to(const char *path, const uint8_t *data, size_t size, int flags) {
    int descriptor = open(path, flags | O_WRONLY | O_CREAT | O_CLOEXEC, smith_world_file_mode);
    if (descriptor < 0) return smith_world_error(errno, "open", path);
    size_t done = 0;
    while (done < size) { // bounded by the size
        ssize_t count = write(descriptor, data + done, size - done);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            int code = count < 0 ? errno : EIO;
            close(descriptor);
            return smith_world_error(code, "write", path);
        }
        done += (size_t)count;
    }
    if (close(descriptor) != 0) return smith_world_error(errno, "close", path);
    return (SmithWorldError){};
}

// Writes the bytes to the file as the mode says. A failed atomic write leaves the old file as it was.
static SmithWorldError smith_world_write(const char *path, const uint8_t *data, size_t size, SmithWorldWrite mode) {
    if (mode == SmithWorld_Append) return smith_world_write_to(path, data, size, O_APPEND);
    if (mode == SmithWorld_Replace) return smith_world_write_to(path, data, size, O_TRUNC);
    char temporary[PATH_MAX];
    int length = snprintf(temporary, sizeof temporary, "%s.tmp", path);
    if (length <= 0 || (size_t)length >= sizeof temporary) return smith_world_error(ENAMETOOLONG, "write", path);
    SmithWorldError error = smith_world_write_to(temporary, data, size, O_TRUNC);
    if (error.code == 0 && rename(temporary, path) != 0) error = smith_world_error(errno, "rename", temporary);
    if (error.code != 0) (void)unlink(temporary);
    return error;
}

// Deletes a file (not a folder: EPERM, as on macOS).
static SmithWorldError smith_world_remove(const char *path) {
    if (unlink(path) == 0) return (SmithWorldError){};
    int code = errno == EISDIR ? EPERM : errno;
    return smith_world_error(code, "remove", path);
}

// Makes the folder and the ones above it that are missing; one that is there already is success.
static SmithWorldError smith_world_make_folder(const char *path) {
    char text[PATH_MAX];
    size_t length = strlen(path);
    if (length == 0 || length >= sizeof text)
        return smith_world_error(length == 0 ? ENOENT : ENAMETOOLONG, "mkdir", path);
    memcpy(text, path, length + 1);
    for (size_t at = 1; at <= length; at++) { // bounded by the path
        if (text[at] != '/' && text[at] != '\0') continue;
        char kept = text[at];
        text[at] = '\0';
        if (mkdir(text, smith_world_folder_mode) != 0 && errno != EEXIST)
            return smith_world_error(errno, "mkdir", text);
        text[at] = kept;
    }
    struct stat status;
    if (stat(path, &status) != 0 || !S_ISDIR(status.st_mode)) return smith_world_error(EEXIST, "mkdir", path);
    return (SmithWorldError){};
}

// What is at the path (symbolic links followed).
static SmithWorldInfo smith_world_info(const char *path) {
    struct stat status;
    if (stat(path, &status) != 0) return (SmithWorldInfo){.error = smith_world_error(errno, "stat", path)};
    bool folder = S_ISDIR(status.st_mode);
    return (SmithWorldInfo){.folder = folder, .size = folder ? 0 : (size_t)status.st_size};
}

// Monotonic time in nanoseconds.
static uint64_t smith_world_clock(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * smith_world_second + (uint64_t)now.tv_nsec;
}

// An arena over address space reserved up front (nothing is committed until used). The memory is never given back:
// the process ends. A failed reservation stops the program.
static SmithArena smith_world_arena(void) {
    void *memory =
        mmap(nullptr, smith_world_reservation, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (memory == MAP_FAILED) {
        fputs("smith: cannot reserve memory for the arena\n", stderr);
        _exit(1);
    }
    return smith_arena_fixed(memory, smith_world_reservation);
}
