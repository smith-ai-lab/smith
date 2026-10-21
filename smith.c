// smith: a drop-in compiler driver.
//
// Milestone 0 is a pure passthrough. It picks a backend by the kind of input, then runs that compiler on the arguments,
// untouched:
//   - a .metal input goes to Apple's Metal compiler: `xcrun -sdk macosx metal`, or $SMITH_METAL if set, which smith
//     replaces itself with;
//   - anything else goes to upstream LLVM's clang. A smith built with SMITH_IN_PROCESS has clang linked in
//     (shim/clang.cpp) and runs clang's own main on the arguments, in its own process, unless $SMITH_CLANG names an
//     external clang to run instead. The stage0, built by one `cc` command, has no clang in it: it replaces itself with
//     an external one, $SMITH_CLANG if set, else Homebrew's llvm (/opt/homebrew/opt/llvm/bin/clang), else the first
//     clang on PATH that is not smith itself. Invoked as `smith++` it uses clang++ ($SMITH_CLANGXX). On macOS it also
//     picks the SDK through the environment, never a flag: see smith_sdk_environment.
// It adds no flags and reads no configuration, so replacing a compiler with smith cannot change a build. See AGENTS.md.
//
// `smith build` compiles the project's build.c with an embedded runtime, runs it to write a ninja manifest, and then
// runs ninja. See docs/build-c.md. `smith fetch <url>` puts a package in the cache and `smith cache` shows and cleans
// the caches. `smith new` writes a ready project: build.c, main.c, the rules and the plugin settings.
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#ifdef SMITH_IN_PROCESS
// shim/clang.cpp, the only C++ in smith: clang's own main, run as `program` (SMITH_LLVM_BIN's clang or clang++) on
// argv[1 .. argc). argv[0] is ignored.
int smith_clang_main(const char *program, int argc, char **argv);
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <stdint.h>
#endif

// On in every build. A failure is a bug: report where and stop.
#define SMITH_ASSERT(condition) \
    do { \
        if (!(condition)) smith_assert_fail(#condition, __FILE__, __LINE__); \
    } while (0)

// Sizes. libc takes int, size_t and char *, so these are plain size_t and int, not the u32 and u64 of game's base.h.
constexpr size_t smith_metal_args_max = 4096; // arguments the metal route can forward; the clang route has no limit
constexpr size_t smith_metal_prefix_max = 4;  // xcrun -sdk macosx metal
constexpr size_t smith_metal_slack = 8;       // room in the argument vector beyond the limit

// Exit statuses are values for exit() and main's return, not alternatives of one type, so they are not an enum.
constexpr int smith_exit_usage = 2;
constexpr int smith_exit_missing = 127;

typedef enum {
    SmithRoute_Clang,
    SmithRoute_Metal,
    // .metal files together with other inputs are refused, for these reasons:
    //   - .metal needs Apple's metal compiler and the rest needs clang, and plain clang cannot compile .metal at all;
    //   - their flags differ (C takes -std=gnu23, frameworks and link flags; Metal takes -std=metal... and its own
    //     options), so one command line cannot be right for both;
    //   - their outputs cannot be one file;
    //   - smith hands each command to exactly one compiler, in its own process or by execv, which is why the exit
    //     status and signals are the compiler's. Running two would need a parent process to wait and combine them.
    SmithRoute_Mixed,
} SmithRoute;

[[noreturn]] static void smith_assert_fail(const char *condition, const char *file, int line) {
    fprintf(stderr, "smith: assertion failed: %s (%s:%d)\n", condition, file, line);
    fflush(stderr);
    abort();
}

[[noreturn]] static void smith_die(int status, const char *message, const char *detail) {
    if (detail != nullptr) {
        fprintf(stderr, "smith: %s: %s\n", message, detail);
    } else {
        fprintf(stderr, "smith: %s\n", message);
    }
    exit(status);
}

static bool smith_ends_with(const char *text, const char *suffix) {
    SMITH_ASSERT(text != nullptr && suffix != nullptr);
    size_t text_length = strlen(text);
    size_t suffix_length = strlen(suffix);
    return text_length >= suffix_length && strcmp(text + text_length - suffix_length, suffix) == 0;
}

// Flags whose value is the next argument, so that the value is not mistaken for an input file (as in `-I
// shaders.metal`).
static bool smith_takes_value(const char *arg) {
    static const char *const flags[] = {
        "-o",         "-I",
        "-isystem",   "-iquote",
        "-idirafter", "-include",
        "-imacros",   "-MF",
        "-MT",        "-MQ",
        "-x",         "-target",
        "-arch",      "-framework",
        "-isysroot",  "-resource-dir",
        "-D",         "-U",
        "-L",         "-l",
        "-F",         "-Xclang",
        "-Xlinker",   "-Xpreprocessor",
        "-mllvm",     "-working-directory",
    };
    for (size_t i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        if (strcmp(arg, flags[i]) == 0) return true;
    }
    return false;
}

// An input the clang route handles, which makes a .metal input beside it a mixed command.
static bool smith_has_other_input_extension(const char *arg) {
    static const char *const extensions[] = {".c", ".h", ".m", ".mm", ".cc", ".cpp", ".cxx", ".C",
                                             ".o", ".a", ".s", ".S",  ".i",  ".ii",  ".pcm", ".dylib"};
    for (size_t i = 0; i < sizeof extensions / sizeof extensions[0]; i++) {
        if (smith_ends_with(arg, extensions[i])) return true;
    }
    return false;
}

// Decides the route from the visible arguments. A response file (@file) is opaque, so it never changes the route.
static SmithRoute smith_classify(int count, char **args) {
    SMITH_ASSERT(count >= 0);
    SMITH_ASSERT(args != nullptr);
    int metal = 0;
    int other = 0;
    for (int i = 0; i < count; i++) { // bounded by count
        const char *arg = args[i];
        SMITH_ASSERT(arg != nullptr); // the first argc entries of argv are never null
        if (arg[0] == '@') continue;
        if (arg[0] == '-') {
            if (smith_takes_value(arg) && i + 1 < count) i += 1;
            continue;
        }
        if (smith_ends_with(arg, ".metal")) {
            metal += 1;
        } else if (smith_has_other_input_extension(arg)) {
            other += 1;
        }
    }
    if (metal > 0 && other > 0) return SmithRoute_Mixed;
    return metal > 0 ? SmithRoute_Metal : SmithRoute_Clang;
}

// The real path of this executable, so a candidate compiler that is smith under another name can be skipped.
static bool smith_self_path(char *out) {
    SMITH_ASSERT(out != nullptr);
    char raw[PATH_MAX];
#ifdef __APPLE__
    uint32_t size = sizeof raw;
    if (_NSGetExecutablePath(raw, &size) != 0) return false;
#else
    ssize_t length = readlink("/proc/self/exe", raw, sizeof raw - 1);
    if (length <= 0) return false;
    raw[length] = '\0';
#endif
    return realpath(raw, out) != nullptr;
}

static bool smith_is_executable(const char *path) {
    struct stat info = {};
    return stat(path, &info) == 0 && S_ISREG(info.st_mode) && access(path, X_OK) == 0;
}

static bool smith_is_not_self(const char *path, const char *self) {
    char real[PATH_MAX];
    return realpath(path, real) != nullptr && strcmp(real, self) != 0;
}

static bool smith_is_usable(const char *path, const char *self) {
    return smith_is_executable(path) && smith_is_not_self(path, self);
}

// The first usable executable named tool in a directory of PATH, written to out.
static bool smith_find_in_path(const char *tool, const char *self, char *out) {
    SMITH_ASSERT(tool != nullptr && self != nullptr && out != nullptr);
    const char *path = getenv("PATH");
    // Each step moves start past one directory of PATH, so the loop ends at the end of the string.
    for (const char *start = path; start != nullptr && *start != '\0';) {
        const char *end = strchr(start, ':');
        size_t length = end != nullptr ? (size_t)(end - start) : strlen(start);
        const char *directory = length > 0 ? start : ".";
        int directory_length = length > 0 ? (int)length : 1;
        int written = snprintf(out, PATH_MAX, "%.*s/%s", directory_length, directory, tool);
        // A directory too long for the buffer is skipped, not a bug: PATH is the user's.
        if (written > 0 && written < PATH_MAX && smith_is_usable(out, self)) return true;
        start = end != nullptr ? end + 1 : nullptr;
    }
    return false;
}

// The compiler for the route: the environment's choice, else Homebrew's LLVM, else (not on macOS) the first on PATH.
static void smith_find_clang(const char *tool, const char *override_name, const char *self, char *out) {
    SMITH_ASSERT(tool != nullptr && override_name != nullptr && self != nullptr && out != nullptr);
    const char *override = getenv(override_name);
    if (override != nullptr && *override != '\0') {
        if (!smith_is_executable(override)) smith_die(smith_exit_missing, "not an executable", override);
        if (!smith_is_not_self(override, self)) smith_die(smith_exit_missing, "refers to smith itself", override);
        int written = snprintf(out, PATH_MAX, "%s", override);
        SMITH_ASSERT(written > 0 && written < PATH_MAX); // stat failed on a path of PATH_MAX or more bytes
        return;
    }
    static const char *const prefixes[] = {"/opt/homebrew/opt/llvm/bin/", "/usr/local/opt/llvm/bin/"};
    for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++) {
        int written = snprintf(out, PATH_MAX, "%s%s", prefixes[i], tool);
        SMITH_ASSERT(written > 0 && written < PATH_MAX); // a short constant prefix and a tool name
        if (smith_is_usable(out, self)) return;
    }
#ifdef __APPLE__
    // No search of PATH on macOS: the clang there would be Apple's, and the owner chose one compiler.
    fprintf(stderr, "smith: cannot find %s in /opt/homebrew/opt/llvm/bin: run `brew install llvm`, or set %s\n", tool,
            override_name);
    exit(smith_exit_missing);
#else
    if (smith_find_in_path(tool, self, out)) return;
    smith_die(smith_exit_missing, "cannot find a compiler; set SMITH_CLANG or SMITH_CLANGXX, tried", tool);
#endif
}

#ifdef __APPLE__
// Runs `xcrun --show-sdk-path` and writes its one line to out. False when xcrun is missing or fails.
static bool smith_xcrun_sdk(const char *self, char *out) {
    char xcrun[PATH_MAX];
    if (!smith_find_in_path("xcrun", self, xcrun)) return false;
    int pipes[2];
    if (pipe(pipes) != 0) return false;
    posix_spawn_file_actions_t actions;
    SMITH_ASSERT(posix_spawn_file_actions_init(&actions) == 0);
    SMITH_ASSERT(posix_spawn_file_actions_adddup2(&actions, pipes[1], 1) == 0);
    SMITH_ASSERT(posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0) == 0);
    SMITH_ASSERT(posix_spawn_file_actions_addclose(&actions, pipes[0]) == 0);
    char *args[] = {xcrun, (char *)"--show-sdk-path", nullptr};
    pid_t pid = 0;
    int spawned = posix_spawn(&pid, xcrun, &actions, nullptr, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipes[1]);
    size_t length = 0;
    if (spawned == 0) {
        ssize_t got = 0;
        // Each read adds to length and the buffer is full at PATH_MAX - 1, so the loop ends.
        while (length < PATH_MAX - 1 && (got = read(pipes[0], out + length, PATH_MAX - 1 - length)) > 0) {
            length += (size_t)got;
        }
        int status = 0;
        if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) length = 0;
    }
    close(pipes[0]);
    while (length > 0 && (out[length - 1] == '\n' || out[length - 1] == '\r'))
        length -= 1;
    out[length] = '\0';
    return length > 0 && strchr(out, '\n') == nullptr;
}

// Homebrew's clang reads a default config that forces -isysroot to the Command Line Tools SDK, which can differ from
// the Xcode SDK that xcrun and metal use, and that -isysroot wins over SDKROOT. So, for each variable the caller has
// not set:
//   - CLANG_NO_DEFAULT_CONFIG=1 turns that config off, so SDKROOT and -isysroot decide;
//   - SDKROOT is what `xcrun --show-sdk-path` says, which is what Apple's clang used.
// The caller's own SDKROOT, -isysroot and --config are never touched. No flag is added.
static void smith_sdk_environment(const char *self) {
    if (getenv("CLANG_NO_DEFAULT_CONFIG") == nullptr) setenv("CLANG_NO_DEFAULT_CONFIG", "1", 0);
    if (getenv("SDKROOT") != nullptr) return;
    char sdk[PATH_MAX];
    if (smith_xcrun_sdk(self, sdk)) setenv("SDKROOT", sdk, 0);
}
#endif

static void smith_show_command(const char *verb, const char *program, char *const *args) {
    fprintf(stderr, "smith: %s %s", verb, program);
    for (size_t i = 1; args[i] != nullptr; i++)
        fprintf(stderr, " %s", args[i]); // args ends with nullptr
    fputc('\n', stderr);
}

static void smith_run(const char *program, char *const *args) {
    SMITH_ASSERT(program != nullptr && args != nullptr);
    SMITH_ASSERT(args[0] == program); // the compiler is handed its own full path as argv[0]
    if (getenv("SMITH_VERBOSE") != nullptr) smith_show_command("exec", program, args);
    execv(program, args);
    smith_die(smith_exit_missing, "cannot execute", program);
}

static const char *smith_base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash != nullptr ? slash + 1 : path;
}

// Fills the start of metal_argv with the program and the arguments that select the Metal compiler, and returns how
// many it used. Apple's is `xcrun -sdk macosx metal`; $SMITH_METAL replaces all four.
static size_t smith_metal_prefix(const char *self, char *program, char **metal_argv) {
    size_t used = 0;
    const char *override = getenv("SMITH_METAL");
    if (override != nullptr && *override != '\0') {
        if (!smith_is_executable(override)) smith_die(smith_exit_missing, "not an executable", override);
        int written = snprintf(program, PATH_MAX, "%s", override);
        SMITH_ASSERT(written > 0 && written < PATH_MAX); // stat failed on a path of PATH_MAX or more bytes
        metal_argv[used++] = program;
    } else {
        if (!smith_find_in_path("xcrun", self, program)) smith_die(smith_exit_missing, "cannot find xcrun", nullptr);
        metal_argv[used++] = program;
        metal_argv[used++] = (char *)"-sdk";
        metal_argv[used++] = (char *)"macosx";
        metal_argv[used++] = (char *)"metal";
    }
    SMITH_ASSERT(used <= smith_metal_prefix_max);
    return used;
}

static int smith_route_metal(int count, char **args, bool which, const char *self) {
    static char *metal_argv[smith_metal_args_max + smith_metal_slack];
    char program[PATH_MAX];
    size_t used = smith_metal_prefix(self, program, metal_argv);
    if (which) {
        printf("metal %s", program);
        for (size_t i = 1; i < used; i++)
            printf(" %s", metal_argv[i]);
        putchar('\n');
        return 0;
    }
    if ((size_t)count + used > smith_metal_args_max) smith_die(smith_exit_usage, "too many arguments", nullptr);
    for (int i = 0; i < count; i++) { // bounded by count, which the check above limits
        SMITH_ASSERT(used < smith_metal_args_max + smith_metal_slack - 1);
        metal_argv[used++] = args[i];
    }
    SMITH_ASSERT(used < smith_metal_args_max + smith_metal_slack); // room for the terminating nullptr
    metal_argv[used] = nullptr;
    smith_run(program, metal_argv);
    return smith_exit_missing;
}

#ifdef SMITH_IN_PROCESS
// Whether clang runs in this process: unless the caller's variable names an external clang to run instead.
static bool smith_runs_in_process(const char *override_name) {
    const char *override = getenv(override_name);
    return override == nullptr || *override == '\0';
}

static int smith_route_in_process(int argc, char **argv, int first, bool which, bool plus, const char *self) {
    char program[PATH_MAX];
    int written = snprintf(program, sizeof program, "%s/%s", SMITH_LLVM_BIN, plus ? "clang++" : "clang");
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof program); // a short constant folder and a tool name
    if (which) {
        printf("clang in-process %s\n", program);
        return 0;
    }
#ifdef __APPLE__
    smith_sdk_environment(self);
#endif
    if (getenv("SMITH_VERBOSE") != nullptr) smith_show_command("in-process", program, argv + first - 1);
    return smith_clang_main(program, argc - first + 1, argv + first - 1);
}
#endif

static int smith_route_clang(int argc, char **argv, int first, bool which, const char *self) {
    SMITH_ASSERT(first >= 1 && argc >= first);
    bool plus = smith_ends_with(smith_base_name(argv[0]), "++");
    const char *tool = plus ? "clang++" : "clang";
#ifdef SMITH_IN_PROCESS
    if (smith_runs_in_process(plus ? "SMITH_CLANGXX" : "SMITH_CLANG")) {
        return smith_route_in_process(argc, argv, first, which, plus, self);
    }
#endif
    char program[PATH_MAX];
    smith_find_clang(tool, plus ? "SMITH_CLANGXX" : "SMITH_CLANG", self, program);
    if (which) {
        printf("clang %s\n", program);
        return 0;
    }
    argv[first - 1] = program; // the full path, so a compiler that finds its own files from argv[0] still can
#ifdef __APPLE__
    smith_sdk_environment(self);
#endif
    smith_run(program, argv + first - 1);
    return smith_exit_missing;
}

// smith build.

// The build program's header and runtime travel inside smith, so `smith build` needs nothing installed beside it and
// `cc -std=gnu23 smith.c -o smith` still builds all of smith.
static const unsigned char smith_build_header[] = {
#embed "include/smith/build.h"
};
static const unsigned char smith_build_runtime[] = {
#embed "runtime/build_runtime.c"
};
// The runtime includes this file, so it is written beside it.
static const unsigned char smith_build_package[] = {
#embed "runtime/package.c"
};

// smith's libraries: each header and source is written into the project's .smith/ on every `smith build`, where build.c
// finds the source with smith_library and the compiles find the header through -I .smith/include.
static const unsigned char smith_arena_header[] = {
#embed "include/smith/arena.h"
};
static const unsigned char smith_arena_source[] = {
#embed "lib/arena.c"
};

static const unsigned char smith_io_header[] = {
#embed "include/smith/io.h"
};
static const unsigned char smith_io_source[] = {
#embed "lib/io.c"
};

static const unsigned char smith_random_header[] = {
#embed "include/smith/random.h"
};
static const unsigned char smith_random_source[] = {
#embed "lib/random.c"
};

static const unsigned char smith_test_header[] = {
#embed "include/smith/test.h"
};
static const unsigned char smith_test_source[] = {
#embed "lib/test.c"
};

typedef struct {
    const char *name;
    const unsigned char *header;
    size_t header_size;
    const unsigned char *source;
    size_t source_size;
} SmithLibrary;

static const SmithLibrary smith_libraries[] = {
    {"arena", smith_arena_header, sizeof smith_arena_header, smith_arena_source, sizeof smith_arena_source},
    {"io", smith_io_header, sizeof smith_io_header, smith_io_source, sizeof smith_io_source},
    {"random", smith_random_header, sizeof smith_random_header, smith_random_source, sizeof smith_random_source},
    {"test", smith_test_header, sizeof smith_test_header, smith_test_source, sizeof smith_test_source},
};

// Packages, shared with the build runtime: the cache, the download and SHA-256.
#include "runtime/package.c"
// `smith cache`, and the lock a build holds on the caches.
#include "runtime/cache.c"

constexpr size_t smith_build_args_max = 4096;
constexpr size_t smith_build_manifest_max = 8192;
constexpr size_t smith_build_chunk = 4096;
constexpr size_t smith_build_ninja_prefix = 3; // ninja -f .smith/build.ninja

static bool smith_file_has_bytes(const char *path, const unsigned char *bytes, size_t size) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return false;
    unsigned char chunk[smith_build_chunk];
    size_t offset = 0;
    bool same = true;
    for (size_t i = 0; same && i <= size / sizeof chunk + 1; i++) { // bounded by the size of the content
        size_t got = fread(chunk, 1, sizeof chunk, file);
        same = got <= size - offset && memcmp(chunk, bytes + offset, got) == 0;
        offset += got;
        if (got < sizeof chunk) break;
    }
    fclose(file);
    return same && offset == size;
}

// Writes the bytes through a temporary file and renames it, only when they differ, so the mtime moves only when the
// content does.
static void smith_write_if_changed(const char *path, const unsigned char *bytes, size_t size) {
    if (smith_file_has_bytes(path, bytes, size)) return;
    char temporary[PATH_MAX];
    int written = snprintf(temporary, sizeof temporary, "%s.tmp", path);
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof temporary); // a short constant path
    FILE *file = fopen(temporary, "wb");
    if (file == nullptr) smith_die(smith_exit_usage, "cannot write", temporary);
    bool ok = fwrite(bytes, 1, size, file) == size;
    ok = fclose(file) == 0 && ok;
    if (!ok || rename(temporary, path) != 0) smith_die(smith_exit_usage, "cannot write", path);
}

static void smith_make_folder(const char *path) {
    if (mkdir(path, 0777) != 0 && errno != EEXIST) smith_die(smith_exit_usage, "cannot create", path);
}

static bool smith_is_plain_word(const char *word) {
    if (*word == '\0') return false;
    for (const char *c = word; *c != '\0'; c++) {
        bool plain = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
                     strchr("_./=:,+@%^-", *c) != nullptr;
        if (!plain) return false;
    }
    return true;
}

// The word as a shell argument inside a ninja command line: quoted unless plain, with $ doubled.
static void smith_quote_word(char *out, size_t capacity, const char *word) {
    size_t used = 0;
    bool quote = !smith_is_plain_word(word);
    SMITH_ASSERT(capacity > 0);
    if (quote) out[used++] = '\'';
    for (const char *c = word; *c != '\0'; c++) {
        const char *piece = *c == '\'' ? "'\\''" : *c == '$' ? "$$" : nullptr;
        size_t length = piece != nullptr ? strlen(piece) : 1;
        SMITH_ASSERT(used + length + 2 < capacity);
        if (piece != nullptr)
            memcpy(out + used, piece, length);
        else
            out[used] = *c;
        used += length;
    }
    if (quote) out[used++] = '\'';
    out[used] = '\0';
}

// The manifest that compiles build.c into .smith/program with the clang `smith` would pick.
static size_t smith_bootstrap_manifest(char *out, size_t capacity, const char *clang) {
    char quoted[PATH_MAX * 2];
    smith_quote_word(quoted, sizeof quoted, clang);
    int written =
        snprintf(out, capacity,
                 "# Written by smith build. It compiles build.c into .smith/program.\n"
                 "ninja_required_version = 1.10\n"
                 "builddir = .smith/bootstrap\n"
                 "rule cc\n"
                 "  command = %s -std=gnu23 -g -O0 -Wall -Wextra -I .smith/include -MMD -MF $out.d -c $in -o $out\n"
                 "  depfile = $out.d\n"
                 "  deps = gcc\n"
                 "  description = smith: compile $in\n"
                 "rule link\n"
                 "  command = %s $in -o $out\n"
                 "  description = smith: link $out\n"
                 "build .smith/bootstrap/build.o: cc build.c\n"
                 "build .smith/bootstrap/runtime.o: cc .smith/runtime.c\n"
                 "build .smith/program: link .smith/bootstrap/build.o .smith/bootstrap/runtime.o\n",
                 quoted, quoted);
    SMITH_ASSERT(written > 0 && (size_t)written < capacity);
    return (size_t)written;
}

static void smith_build_find_ninja(const char *self, char *out) {
    const char *override = getenv("SMITH_NINJA");
    if (override != nullptr && *override != '\0') {
        int written = snprintf(out, PATH_MAX, "%s", override);
        if (written > 0 && written < PATH_MAX && smith_is_executable(out)) return;
    } else if (smith_find_in_path("ninja", self, out)) {
        return;
    }
    smith_die(smith_exit_missing, "cannot find ninja; install it or set SMITH_NINJA", nullptr);
}

// Runs a program as a child and waits; the result is its exit status, or 128 plus the signal that killed it.
static int smith_spawn_with(const char *program, char *const *args, const posix_spawn_file_actions_t *actions) {
    SMITH_ASSERT(program != nullptr && args != nullptr);
    if (getenv("SMITH_VERBOSE") != nullptr) smith_show_command("run", program, args);
    pid_t pid = 0;
    if (posix_spawn(&pid, program, actions, nullptr, args, environ) != 0) {
        smith_die(smith_exit_missing, "cannot execute", program);
    }
    int status = 0;
    int waited = -1;
    for (int i = 0; i < 1000 && waited < 0; i++) // bounded; only a signal interrupts the wait
        waited = waitpid(pid, &status, 0);
    if (waited < 0) smith_die(smith_exit_missing, "cannot wait for", program);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128 + WTERMSIG(status);
}

static int smith_build_spawn(const char *program, char *const *args) {
    return smith_spawn_with(program, args, nullptr);
}

// Splits the arguments after `build`: -D goes to the build program, the rest to ninja, which is told which file to read
// by smith alone. The two vectors are filled after their fixed starts and end with nullptr.
static void smith_build_split(int count, char **args, char **ninja_argv, char **program_argv, size_t *ninja_used,
                              size_t *program_used) {
    for (int i = 0; i < count; i++) { // bounded by count, which main limits
        const char *arg = args[i];
        if (strncmp(arg, "-f", 2) == 0 || strncmp(arg, "-C", 2) == 0) {
            smith_die(smith_exit_usage, "smith build names the manifest itself; run it from the project folder",
                      nullptr);
        }
        if (strncmp(arg, "-D", 2) == 0) {
            program_argv[(*program_used)++] = args[i];
        } else {
            ninja_argv[(*ninja_used)++] = args[i];
        }
    }
    ninja_argv[*ninja_used] = nullptr;
    program_argv[*program_used] = nullptr;
}

// Writes each library's header to .smith/include/smith/<name>.h and its source to .smith/lib/<name>.c.
static void smith_build_write_libraries(void) {
    for (size_t i = 0; i < sizeof smith_libraries / sizeof smith_libraries[0]; i++) {
        const SmithLibrary *library = &smith_libraries[i];
        char path[PATH_MAX];
        int written = snprintf(path, sizeof path, ".smith/include/smith/%s.h", library->name);
        SMITH_ASSERT(written > 0 && (size_t)written < sizeof path);
        smith_write_if_changed(path, library->header, library->header_size);
        written = snprintf(path, sizeof path, ".smith/lib/%s.c", library->name);
        SMITH_ASSERT(written > 0 && (size_t)written < sizeof path);
        smith_write_if_changed(path, library->source, library->source_size);
    }
}

static void smith_build_prepare(const char *clang) {
    smith_make_folder(".smith");
    smith_make_folder(".smith/include");
    smith_make_folder(".smith/include/smith");
    smith_make_folder(".smith/lib");
    smith_make_folder(".smith/bootstrap");
    static char manifest[smith_build_manifest_max];
    size_t length = smith_bootstrap_manifest(manifest, sizeof manifest, clang);
    // Git ignores smith's folder with no change to the project, as pytest, ruff and mypy do for their caches.
    smith_write_if_changed(".smith/.gitignore", (const unsigned char *)"*\n", 2);
    smith_write_if_changed(".smith/include/smith/build.h", smith_build_header, sizeof smith_build_header);
    smith_build_write_libraries();
    smith_write_if_changed(".smith/runtime.c", smith_build_runtime, sizeof smith_build_runtime);
    smith_write_if_changed(".smith/package.c", smith_build_package, sizeof smith_build_package);
    smith_write_if_changed(".smith/bootstrap/build.ninja", (const unsigned char *)manifest, length);
}

// What compiles and links build.c: an external clang, found as for any compile, or smith itself when clang is in it.
static void smith_build_compiler(const char *self, char *out) {
#ifdef SMITH_IN_PROCESS
    if (smith_runs_in_process("SMITH_CLANG")) {
        int written = snprintf(out, PATH_MAX, "%s", self);
        SMITH_ASSERT(written > 0 && written < PATH_MAX); // self came from a PATH_MAX buffer
        return;
    }
#endif
    smith_find_clang("clang", "SMITH_CLANG", self, out);
}

static int smith_build_main(int count, char **args, const char *self) {
    SMITH_ASSERT(count >= 0);
    if (access("build.c", R_OK) != 0) smith_die(smith_exit_usage, "no build.c in this folder", nullptr);
    smith_cache_hold();
    char ninja[PATH_MAX];
    smith_build_find_ninja(self, ninja);
    if ((size_t)count > smith_build_args_max) smith_die(smith_exit_usage, "too many arguments", nullptr);
    static char *ninja_argv[smith_build_args_max + smith_build_ninja_prefix + 1];
    static char *program_argv[smith_build_args_max + 3];
    ninja_argv[0] = ninja;
    ninja_argv[1] = (char *)"-f";
    ninja_argv[2] = (char *)".smith/build.ninja";
    program_argv[0] = (char *)".smith/program";
    program_argv[1] = (char *)".smith/build.ninja";
    size_t ninja_used = smith_build_ninja_prefix;
    size_t program_used = 2;
    smith_build_split(count, args, ninja_argv, program_argv, &ninja_used, &program_used);

    char clang[PATH_MAX];
    smith_build_compiler(self, clang);
#ifdef __APPLE__
    smith_sdk_environment(self); // ninja and the compiles it starts inherit it, as do the programs build.c runs
#endif
    smith_build_prepare(clang);
    char *bootstrap_argv[] = {ninja, (char *)"--quiet", (char *)"-f", (char *)".smith/bootstrap/build.ninja", nullptr};
    int status = smith_build_spawn(ninja, bootstrap_argv);
    if (status == 0) status = smith_build_spawn(program_argv[0], program_argv);
    if (status != 0) return status;
    smith_run(ninja, ninja_argv);
    return smith_exit_missing;
}

// smith fetch.

// `smith fetch <url>` downloads an archive into the package cache and prints the line to paste into build.c.
static int smith_fetch_main(int count, char **args) {
    if (count != 1) smith_die(smith_exit_usage, "usage: smith fetch <url>", nullptr);
    char folder[PATH_MAX];
    char hash[smith_package_hash_size];
    SmithPackageError error;
    if (!smith_package_ensure(args[0], nullptr, false, folder, hash, &error)) {
        smith_die(smith_exit_usage, "fetch failed:", error.text);
    }
    printf(".hash = \"%s\",\n", hash);
    fflush(stdout);
    fprintf(stderr, "smith: %s is in %s\n", args[0], folder);
    return 0;
}

// `smith --smith-sha256 <file>` prints a file's SHA-256, which the tests check against published vectors.
static int smith_sha256_main(int count, char **args) {
    if (count != 1) smith_die(smith_exit_usage, "usage: smith --smith-sha256 <file>", nullptr);
    char hex[smith_sha256_hex_size];
    if (!smith_sha256_file(args[0], hex)) smith_die(smith_exit_usage, "cannot read", args[0]);
    puts(hex);
    return 0;
}

// smith new.

// The files a new project gets travel inside smith, so a project gets exactly what the installed smith ships. Agents
// get smith's skills from the smith plugin, which .claude/settings.json enables. The dotfiles are stored without their
// dot so that they do not act on smith's own repository.
static const unsigned char smith_new_build_c[] = {
#embed "templates/new/build.c"
};
static const unsigned char smith_new_main_c[] = {
#embed "templates/new/main.c"
};
static const unsigned char smith_new_gitignore[] = {
#embed "templates/new/gitignore"
};
static const unsigned char smith_new_clang_format[] = {
#embed "templates/new/clang-format"
};
static const unsigned char smith_new_clang_tidy[] = {
#embed "templates/new/clang-tidy"
};
static const unsigned char smith_new_claude_settings[] = {
#embed "templates/new/claude-settings.json"
};

typedef struct {
    const char *path;
    const unsigned char *bytes;
    size_t size;
    bool named; // @NAME@ is replaced by the program's name
} SmithNewFile;

static const SmithNewFile smith_new_files[] = {
    {"build.c", smith_new_build_c, sizeof smith_new_build_c, true},
    {"main.c", smith_new_main_c, sizeof smith_new_main_c, true},
    {".gitignore", smith_new_gitignore, sizeof smith_new_gitignore, false},
    {".clang-format", smith_new_clang_format, sizeof smith_new_clang_format, false},
    {".clang-tidy", smith_new_clang_tidy, sizeof smith_new_clang_tidy, false},
    {".claude/settings.json", smith_new_claude_settings, sizeof smith_new_claude_settings, false},
};

constexpr size_t smith_new_name_max = 200;
static const char smith_new_placeholder[] = "@NAME@";
constexpr size_t smith_new_placeholder_length = sizeof smith_new_placeholder - 1;
constexpr size_t smith_new_written_max = 16;
constexpr size_t smith_new_gitignore_lines_max = 100000;

typedef struct {
    int created;
    int kept;
    const char *written[smith_new_written_max]; // the paths created, for git
    size_t written_count;
} SmithNewCounts;

// A name is letters, digits, _, . and -, and does not start with . or -.
static bool smith_new_is_name(const char *name) {
    size_t length = strlen(name);
    if (length == 0 || length > smith_new_name_max) return false;
    for (size_t i = 0; i < length; i++) { // bounded by the length
        char c = name[i];
        bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!word && (i == 0 || (c != '.' && c != '-'))) return false;
    }
    return true;
}

[[noreturn]] static void smith_new_bad_name(const char *name, bool from_folder) {
    fprintf(stderr, "smith: %s cannot name a program; use letters, digits, _, . and -%s\n", name,
            from_folder ? "; smith new <name> from the parent folder picks another name" : "");
    exit(smith_exit_usage);
}

// Writes the bytes to a path nothing exists at, with @NAME@ replaced when the file is named.
static void smith_new_write(const SmithNewFile *file, const char *name) {
    FILE *out = fopen(file->path, "wbx");
    if (out == nullptr) smith_die(smith_exit_usage, "cannot write", file->path);
    bool ok = true;
    for (size_t i = 0; i < file->size; i++) { // bounded by the size of the content
        bool hole = file->named && file->size - i >= smith_new_placeholder_length &&
                    memcmp(file->bytes + i, smith_new_placeholder, smith_new_placeholder_length) == 0;
        if (hole) {
            ok = fputs(name, out) >= 0 && ok;
            i += smith_new_placeholder_length - 1;
        } else {
            ok = fputc(file->bytes[i], out) != EOF && ok;
        }
    }
    ok = fclose(out) == 0 && ok;
    if (!ok) smith_die(smith_exit_usage, "cannot write", file->path);
}

static bool smith_new_exists(const char *path) {
    struct stat info = {};
    if (lstat(path, &info) == 0) return true;
    if (errno != ENOENT) smith_die(smith_exit_usage, "cannot inspect", path);
    return false;
}

// Makes the folders above a path, one at a time.
static void smith_new_make_parents(const char *path) {
    char partial[PATH_MAX];
    int written = snprintf(partial, sizeof partial, "%s", path);
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof partial);
    for (char *slash = strchr(partial, '/'); slash != nullptr; slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        smith_make_folder(partial);
        *slash = '/';
    }
}

static void smith_new_report(const char *verb, const char *path) {
    printf("  %-8s %s\n", verb, path);
}

// What the folder already builds with, if it is not smith: the first of these files that exists.
static const char *smith_new_other_build(void) {
    static const char *const files[] = {"Makefile", "makefile", "GNUmakefile", "CMakeLists.txt"};
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        if (smith_new_exists(files[i])) return files[i];
    }
    return nullptr;
}

static void smith_new_count(SmithNewCounts *counts, const char *path, bool exists) {
    smith_new_report(exists ? "kept" : "created", path);
    if (exists) {
        counts->kept += 1;
        return;
    }
    counts->created += 1;
    SMITH_ASSERT(counts->written_count < smith_new_written_max);
    counts->written[counts->written_count++] = path;
}

// Each file is written only if nothing exists at its path. main.c is left out when the project has its own sources: a
// build.c already there, or another build system.
static void smith_new_files_write(const char *name, bool skip_main, SmithNewCounts *counts) {
    for (size_t i = 0; i < sizeof smith_new_files / sizeof smith_new_files[0]; i++) {
        const SmithNewFile *file = &smith_new_files[i];
        if (skip_main && strcmp(file->path, "main.c") == 0) continue;
        bool exists = smith_new_exists(file->path);
        if (!exists) {
            smith_new_make_parents(file->path);
            smith_new_write(file, name);
        }
        smith_new_count(counts, file->path, exists);
    }
}

// The lines of smith's .gitignore that an existing one lacks, said so that the user can add them. A line is present
// only if it matches exactly, ignoring trailing spaces.
static void smith_new_gitignore_missing(void) {
    static const char *const wanted[] = {"/build/", "/compile_commands.json", "/.cache/"};
    bool present[sizeof wanted / sizeof wanted[0]] = {};
    FILE *file = fopen(".gitignore", "r");
    if (file == nullptr) return;
    char line[PATH_MAX];
    for (size_t read = 0; read < smith_new_gitignore_lines_max && fgets(line, sizeof line, file) != nullptr; read++) {
        size_t length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r' || line[length - 1] == ' ')) {
            line[--length] = '\0';
        }
        for (size_t i = 0; i < sizeof wanted / sizeof wanted[0]; i++) {
            if (strcmp(line, wanted[i]) == 0) present[i] = true;
        }
    }
    fclose(file);
    for (size_t i = 0; i < sizeof wanted / sizeof wanted[0]; i++) {
        if (!present[i]) printf("  .gitignore lacks %s; add it\n", wanted[i]);
    }
}

// Says so when an existing .claude/settings.json does not mention the smith plugin, so that the user can add it.
static void smith_new_settings_missing(void) {
    FILE *file = fopen(".claude/settings.json", "r");
    if (file == nullptr) return;
    char line[PATH_MAX];
    bool present = false;
    for (size_t read = 0; read < smith_new_gitignore_lines_max && fgets(line, sizeof line, file) != nullptr; read++) {
        if (strstr(line, "\"smith@smith\"") != nullptr) present = true;
    }
    fclose(file);
    if (!present) {
        printf("  .claude/settings.json lacks \"smith@smith\"; add \"smith@smith\": true under enabledPlugins to give\n"
               "  agents smith's skills\n");
    }
}

// Runs git with its output silenced as asked: stdout, or stdout and stderr.
static int smith_new_git(const char *git, bool silence_errors, char *const *args) {
    posix_spawn_file_actions_t actions;
    SMITH_ASSERT(posix_spawn_file_actions_init(&actions) == 0);
    SMITH_ASSERT(posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0) == 0);
    if (silence_errors) SMITH_ASSERT(posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0) == 0);
    int status = smith_spawn_with(git, args, &actions);
    posix_spawn_file_actions_destroy(&actions);
    return status;
}

// Puts the new project under git on main, unless git is missing or the folder is already in a work tree. A failed
// commit, for example with no user.name, leaves the files staged. Returns the line for the summary.
static const char *smith_new_commit(const char *self, const char *name, const SmithNewCounts *only) {
    char git[PATH_MAX];
    if (!smith_find_in_path("git", self, git)) return "skipped: git is not on PATH";
    char *inside[] = {git, (char *)"rev-parse", (char *)"--is-inside-work-tree", nullptr};
    if (smith_new_git(git, true, inside) == 0) return "kept: the folder is already inside a git work tree";
    char message[PATH_MAX];
    int written = snprintf(message, sizeof message, "chore: create %s with smith new", name);
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof message);
    char *init[] = {git, (char *)"init", (char *)"-q", (char *)"-b", (char *)"main", nullptr};
    // Beside another build system only the files written are added; the rest of the folder is the owner's.
    char *add[smith_new_written_max + 4] = {git, (char *)"add", (char *)"-A", nullptr};
    if (only != nullptr) {
        add[2] = (char *)"--";
        for (size_t i = 0; i < only->written_count; i++)
            add[3 + i] = (char *)only->written[i];
        add[3 + only->written_count] = nullptr;
    }
    char *commit[] = {git, (char *)"commit", (char *)"-q", (char *)"-m", message, nullptr};
    if (smith_new_git(git, false, init) != 0) return "failed: git init; no repository was made";
    if (smith_new_git(git, false, add) != 0) return "failed: git add; the repository has no commit";
    if (smith_new_git(git, false, commit) != 0) {
        return "commit skipped: git could not commit (is user.name and user.email set?); the files are staged";
    }
    return "created a repository on main with one commit";
}

// Moves into the folder of the project, making it if needed, and returns its name. With no argument the folder is the
// current one and its own name is the program's.
static const char *smith_new_enter(int count, char **args, char *folder_name) {
    if (count == 1) {
        if (!smith_new_is_name(args[0])) smith_new_bad_name(args[0], false);
        smith_make_folder(args[0]);
        if (chdir(args[0]) != 0) smith_die(smith_exit_usage, "cannot enter", args[0]);
        return args[0];
    }
    char here[PATH_MAX];
    if (getcwd(here, sizeof here) == nullptr) smith_die(smith_exit_usage, "cannot find the current folder", nullptr);
    int written = snprintf(folder_name, PATH_MAX, "%s", smith_base_name(here));
    SMITH_ASSERT(written >= 0 && written < PATH_MAX);
    if (!smith_new_is_name(folder_name)) smith_new_bad_name(folder_name, true);
    return folder_name;
}

static int smith_new_main(int count, char **args, const char *self) {
    SMITH_ASSERT(count >= 0);
    if (count > 1) smith_die(smith_exit_usage, "new takes at most one argument", "smith new [<name>]");
    char folder_name[PATH_MAX];
    const char *name = smith_new_enter(count, args, folder_name);
    printf("smith new: %s\n", name);
    bool has_build_c = smith_new_exists("build.c");
    const char *other = has_build_c ? nullptr : smith_new_other_build();
    bool had_gitignore = smith_new_exists(".gitignore");
    bool had_settings = smith_new_exists(".claude/settings.json");
    SmithNewCounts counts = {};
    smith_new_files_write(name, has_build_c || other != nullptr, &counts);
    fflush(stdout);
    // The template compiles a main.c that a project with another build system does not have, so it is not built.
    if (other == nullptr) {
        char *build_argv[] = {(char *)self, (char *)"build", nullptr};
        if (smith_build_spawn(self, build_argv) != 0) {
            smith_die(smith_exit_usage,
                      "the first build failed, so nothing was committed; fix it, then run smith build", nullptr);
        }
    }
    const char *git = smith_new_commit(self, name, other != nullptr ? &counts : nullptr);
    printf("\nsmith new: %s: %d created, %d kept\n  git: %s\n", name, counts.created, counts.kept, git);
    if (had_gitignore) smith_new_gitignore_missing();
    if (had_settings) smith_new_settings_missing();
    printf("\nAgents get smith's skills from the smith plugin. If it is not installed:\n"
           "  /plugin marketplace add adam-r-kowalski/smith\n  /plugin install smith@smith\n");
    if (other != nullptr) {
        printf("\nThis project builds with a %s. build.c is a starting point: port the %s into it, following the\n"
               "smith-build skill, and remove the %s once smith build gives\n"
               "the same results.\n",
               other, other, other);
        return 0;
    }
    printf("\nNext:\n");
    if (count == 1) printf("  cd %s\n", name);
    printf("  smith build run\n");
    return 0;
}

int main(int argc, char **argv) {
    SMITH_ASSERT(argc >= 1 && argv[0] != nullptr);
    if (argc >= 2 && strcmp(argv[1], "--smith-version") == 0) {
        puts("smith 0.0 (milestone 0: a routing passthrough)");
        return 0;
    }
    bool which = argc >= 2 && strcmp(argv[1], "--smith-which") == 0;
    int first = which ? 2 : 1;
    int count = argc - first;
    char **args = argv + first;

    char self[PATH_MAX];
    if (!smith_self_path(self)) smith_die(smith_exit_missing, "cannot determine the path of this executable", nullptr);

    if (argc >= 2 && strcmp(argv[1], "build") == 0) return smith_build_main(argc - 2, argv + 2, self);
    if (argc >= 2 && strcmp(argv[1], "cache") == 0) return smith_cache_main(argc - 2, argv + 2);
    if (argc >= 2 && strcmp(argv[1], "fetch") == 0) return smith_fetch_main(argc - 2, argv + 2);
    if (argc >= 2 && strcmp(argv[1], "--smith-sha256") == 0) return smith_sha256_main(argc - 2, argv + 2);
    if (argc >= 2 && strcmp(argv[1], "new") == 0) return smith_new_main(argc - 2, argv + 2, self);

    switch (smith_classify(count, args)) {
    case SmithRoute_Mixed:
        smith_die(
            smith_exit_usage,
            ".metal and other inputs need different compilers and flags; compile the shaders in their own command",
            nullptr);
    case SmithRoute_Metal:
        return smith_route_metal(count, args, which, self);
    case SmithRoute_Clang:
        return smith_route_clang(argc, argv, first, which, self);
    }
    return smith_exit_missing;
}
