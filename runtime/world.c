// What the executor, `smith mcp` and the verbs do to the world besides running processes: read, write, remove and
// make files, ask about a path, read the clock and reserve memory. Plain calls into libc (open, read, write, rename,
// unlink, mkdir, stat, clock_gettime, mmap), so a handler table interposes them as it does for any C code: there is no
// value to pass and nothing here to swap (owner, 2026-11-06, #2657: "we want io FULLY gone"). Errors are values: an
// errno code and a message, as `open config.txt: No such file or directory`.
//
// The read puts the bytes in the arena it is given, with a zero after them that the size does not count, so a caller
// can hand the text to C. A user of this file takes what it needs: the rest is `maybe_unused`.

#pragma once

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <smith/arena.h>
#include <smith/handlers.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

// What is at the path (symbolic links followed).
static SmithWorldInfo smith_world_info(const char *path) {
    SmithFileHandler *handler = smith_handlers_files();
    if (handler != nullptr && handler->stat != nullptr) {
        SmithFileStatus fake;
        if (handler->stat(handler, path, &fake) != 0)
            return (SmithWorldInfo){.error = smith_world_error(errno, "stat", path)};
        return (SmithWorldInfo){.folder = fake.folder, .size = fake.folder ? 0 : fake.size};
    }
    struct stat status;
    if (stat(path, &status) != 0) return (SmithWorldInfo){.error = smith_world_error(errno, "stat", path)};
    bool folder = S_ISDIR(status.st_mode);
    return (SmithWorldInfo){.folder = folder, .size = folder ? 0 : (size_t)status.st_size};
}

// A whole file in the arena. A name that is not a file is the error of its open or read. With a files handler set
// (smith_handlers_set_files) its members do the work, so a test can swap the file system.
static SmithWorldRead smith_world_read(SmithArena *arena, const char *path) {
    SmithFileHandler *handler = smith_handlers_files();
    SmithWorldInfo info = smith_world_info(path);
    if (info.error.code != 0) return (SmithWorldRead){.error = info.error};
    if (info.folder) return (SmithWorldRead){.error = smith_world_error(EISDIR, "read", path)};
    uint8_t *data = smith_arena_push(arena, info.size + 1, 1);
    int descriptor = handler != nullptr ? handler->open(handler, path, O_RDONLY, 0) : open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) return (SmithWorldRead){.error = smith_world_error(errno, "open", path)};
    size_t used = 0;
    int code = 0;
    while (used < info.size) { // bounded by the size the file had
        ssize_t count = handler != nullptr ? handler->read(handler, descriptor, data + used, info.size - used)
                                           : read(descriptor, data + used, info.size - used);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) code = errno;
        if (count <= 0) break;
        used += (size_t)count;
    }
    if (handler != nullptr)
        (void)handler->close(handler, descriptor);
    else
        close(descriptor);
    data[used] = 0;
    if (code != 0) return (SmithWorldRead){.error = smith_world_error(code, "read", path)};
    return (SmithWorldRead){.data = data, .size = used};
}

// Deletes a file (not a folder: EPERM, as on macOS).
static SmithWorldError smith_world_remove(const char *path) {
    SmithFileHandler *handler = smith_handlers_files();
    if (handler != nullptr && handler->remove != nullptr) {
        if (handler->remove(handler, path) == 0) return (SmithWorldError){};
        return smith_world_error(errno, "remove", path);
    }
    if (unlink(path) == 0) return (SmithWorldError){};
    int code = errno == EISDIR ? EPERM : errno;
    return smith_world_error(code, "remove", path);
}

static SmithWorldError smith_world_write_to(const char *path, const uint8_t *data, size_t size, int flags) {
    SmithFileHandler *handler = smith_handlers_files();
    int mode = flags | O_WRONLY | O_CREAT;
    int descriptor = handler != nullptr ? handler->open(handler, path, mode, smith_world_file_mode)
                                        : open(path, mode | O_CLOEXEC, smith_world_file_mode);
    if (descriptor < 0) return smith_world_error(errno, "open", path);
    size_t done = 0;
    while (done < size) { // bounded by the size
        ssize_t count = handler != nullptr ? handler->write(handler, descriptor, data + done, size - done)
                                           : write(descriptor, data + done, size - done);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            int code = count < 0 ? errno : EIO;
            if (handler != nullptr)
                (void)handler->close(handler, descriptor);
            else
                close(descriptor);
            return smith_world_error(code, "write", path);
        }
        done += (size_t)count;
    }
    int closed = handler != nullptr ? handler->close(handler, descriptor) : close(descriptor);
    if (closed != 0) return smith_world_error(errno, "close", path);
    return (SmithWorldError){};
}

// Writes the bytes to the file as the mode says. A failed atomic write leaves the old file as it was.
[[maybe_unused]] static SmithWorldError smith_world_write(const char *path, const uint8_t *data, size_t size,
                                                          SmithWorldWrite mode) {
    if (mode == SmithWorld_Append) return smith_world_write_to(path, data, size, O_APPEND);
    if (mode == SmithWorld_Replace) return smith_world_write_to(path, data, size, O_TRUNC);
    char temporary[PATH_MAX];
    int length = snprintf(temporary, sizeof temporary, "%s.tmp", path);
    if (length <= 0 || (size_t)length >= sizeof temporary) return smith_world_error(ENAMETOOLONG, "write", path);
    SmithWorldError error = smith_world_write_to(temporary, data, size, O_TRUNC);
    SmithFileHandler *handler = smith_handlers_files();
    if (error.code == 0) {
        bool handled = handler != nullptr && handler->rename != nullptr;
        if ((handled ? handler->rename(handler, temporary, path) : rename(temporary, path)) != 0)
            error = smith_world_error(errno, "rename", temporary);
    }
    if (error.code != 0) (void)smith_world_remove(temporary);
    return error;
}

// Makes the folder and the ones above it that are missing; one that is there already is success.
[[maybe_unused]] static SmithWorldError smith_world_make_folder(const char *path) {
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
    SmithWorldInfo info = smith_world_info(path);
    if (info.error.code != 0 || !info.folder) return smith_world_error(EEXIST, "mkdir", path);
    return (SmithWorldError){};
}

// Monotonic time in nanoseconds.
[[maybe_unused]] static uint64_t smith_world_clock(void) {
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

extern char **environ;

// Runs the program (found on PATH) to its end with its stdout in buffer: the number of bytes, or -1 when it cannot
// start, fails to read or exits with a status other than 0, and -2 when it prints a buffer's worth or more (the child
// is then killed by the closed pipe, and its status is not asked). Its stderr is discarded. One blocking run: waiting
// on many processes at once is the task runtime's.
[[maybe_unused]] static ptrdiff_t smith_world_capture(const char *const *argv, char *buffer, size_t capacity) {
    int pipes[2];
    if (pipe(pipes) != 0) return -1;
    posix_spawn_file_actions_t actions;
    int setup = posix_spawn_file_actions_init(&actions);
    if (setup == 0) setup = posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO);
    if (setup == 0) setup = posix_spawn_file_actions_addclose(&actions, pipes[0]);
    if (setup == 0) setup = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    if (setup == 0) setup = posix_spawn_file_actions_addclose(&actions, pipes[1]);
    pid_t pid = 0;
    if (setup == 0) setup = posix_spawnp(&pid, argv[0], &actions, nullptr, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipes[1]);
    ptrdiff_t used = setup == 0 ? 0 : -1;
    while (used >= 0) { // bounded by the capacity: every read moves at least a byte or ends the loop
        if ((size_t)used >= capacity) {
            close(pipes[0]);
            (void)waitpid(pid, nullptr, 0);
            return -2;
        }
        ssize_t count = read(pipes[0], buffer + used, capacity - (size_t)used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            if (count < 0) used = -1;
            break;
        }
        used += count;
    }
    close(pipes[0]);
    if (setup != 0) return -1;
    int status = 0;
    pid_t waited = 0;
    do
        waited = waitpid(pid, &status, 0); // NOLINT(bugprone-signal-handler)
    while (waited < 0 && errno == EINTR);
    return waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? used : -1;
}
