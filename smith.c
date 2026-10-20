// smith: a drop-in compiler driver.
//
// Milestone 0 is a pure passthrough. It picks a backend by the kind of input, then replaces itself with that compiler
// and hands over the arguments untouched:
//   - a .metal input goes to Apple's Metal compiler: `xcrun -sdk macosx metal`, or $SMITH_METAL if set;
//   - anything else goes to the platform's best clang: $SMITH_CLANG if set, else Apple's on macOS (/usr/bin/clang runs
//     it through xcrun, which also finds the SDK), else Homebrew's LLVM, else the first clang on PATH that is not smith
//     itself. Invoked as `smith++` it uses clang++ instead ($SMITH_CLANGXX).
// It adds no flags and reads no configuration, so replacing a compiler with smith cannot change a build. A .smith input
// is where the extra behavior will live; for now it is an error. See AGENTS.md.
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
    SmithRoute_Mixed, // .metal files together with other inputs: they need different compilers
    SmithRoute_Smith, // a .smith input
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
    int smith = 0;
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
        } else if (smith_ends_with(arg, ".smith")) {
            smith += 1;
        } else if (smith_has_other_input_extension(arg)) {
            other += 1;
        }
    }
    if (smith > 0) return SmithRoute_Smith;
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

// The compiler for the route: the environment's choice, else Apple's clang on macOS, else Homebrew's LLVM, else PATH.
// Apple's clang is the default there because upstream's AddressSanitizer costs about 40 ms per process on macOS.
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
    static const char *const prefixes[] = {
#ifdef __APPLE__
        "/usr/bin/",
#endif
        "/opt/homebrew/opt/llvm/bin/",
        "/usr/local/opt/llvm/bin/",
    };
    for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++) {
        int written = snprintf(out, PATH_MAX, "%s%s", prefixes[i], tool);
        SMITH_ASSERT(written > 0 && written < PATH_MAX); // a short constant prefix and a tool name
        if (smith_is_usable(out, self)) return;
    }
    if (smith_find_in_path(tool, self, out)) return;
    smith_die(smith_exit_missing, "cannot find a compiler; set SMITH_CLANG or SMITH_CLANGXX, tried", tool);
}

static void smith_show_command(const char *program, char *const *args) {
    fprintf(stderr, "smith: exec %s", program);
    for (size_t i = 1; args[i] != nullptr; i++)
        fprintf(stderr, " %s", args[i]); // args ends with nullptr
    fputc('\n', stderr);
}

static void smith_run(const char *program, char *const *args) {
    SMITH_ASSERT(program != nullptr && args != nullptr);
    SMITH_ASSERT(args[0] == program); // the compiler is handed its own full path as argv[0]
    if (getenv("SMITH_VERBOSE") != nullptr) smith_show_command(program, args);
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

static int smith_route_clang(char **argv, int first, bool which, const char *self) {
    SMITH_ASSERT(first >= 1);
    bool plus = smith_ends_with(smith_base_name(argv[0]), "++");
    const char *tool = plus ? "clang++" : "clang";
    char program[PATH_MAX];
    smith_find_clang(tool, plus ? "SMITH_CLANGXX" : "SMITH_CLANG", self, program);
    if (which) {
        printf("clang %s\n", program);
        return 0;
    }
    argv[first - 1] = program; // the full path, so a compiler that finds its own files from argv[0] still can
    smith_run(program, argv + first - 1);
    return smith_exit_missing;
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

    switch (smith_classify(count, args)) {
    case SmithRoute_Smith:
        smith_die(smith_exit_usage, ".smith inputs are not supported yet; milestone 0 is a passthrough", nullptr);
    case SmithRoute_Mixed:
        smith_die(smith_exit_usage, "cannot compile .metal files together with other inputs in one command; split it",
                  nullptr);
    case SmithRoute_Metal:
        return smith_route_metal(count, args, which, self);
    case SmithRoute_Clang:
        return smith_route_clang(argv, first, which, self);
    }
    return smith_exit_missing;
}
