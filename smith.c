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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <stdint.h>
#endif

enum { smith_metal_args_max = 4096 }; // arguments the metal route can forward; the clang route has no limit
enum { smith_exit_usage = 2, smith_exit_missing = 127 };

typedef enum {
    Route_Clang,
    Route_Metal,
    Route_Mixed, // .metal files together with other inputs: they need different compilers
    Route_Smith, // a .smith input
} Route;

__attribute__((noreturn)) static void smith_die(int status, const char *message, const char *detail) {
    if (detail != NULL) {
        fprintf(stderr, "smith: %s: %s\n", message, detail);
    } else {
        fprintf(stderr, "smith: %s\n", message);
    }
    exit(status);
}

static bool ends_with(const char *text, const char *suffix) {
    size_t text_length = strlen(text);
    size_t suffix_length = strlen(suffix);
    return text_length >= suffix_length && strcmp(text + text_length - suffix_length, suffix) == 0;
}

// Flags whose value is the next argument, so that the value is not mistaken for an input file (as in `-I shaders.metal`).
static bool takes_value(const char *arg) {
    static const char *const flags[] = {
        "-o",     "-I",          "-isystem",      "-iquote",       "-idirafter", "-include",    "-imacros",
        "-MF",    "-MT",         "-MQ",           "-x",            "-target",    "-arch",       "-framework",
        "-isysroot", "-resource-dir", "-D",       "-U",            "-L",         "-l",          "-F",
        "-Xclang", "-Xlinker",   "-Xpreprocessor", "-mllvm",       "-working-directory",
    };
    for (size_t i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        if (strcmp(arg, flags[i]) == 0) return true;
    }
    return false;
}

// An input the clang route handles, which makes a .metal input beside it a mixed command.
static bool has_other_input_extension(const char *arg) {
    static const char *const extensions[] = {".c",  ".h",  ".m",  ".mm", ".cc", ".cpp", ".cxx", ".C",
                                             ".o",  ".a",  ".s",  ".S",  ".i",  ".ii",  ".pcm", ".dylib"};
    for (size_t i = 0; i < sizeof extensions / sizeof extensions[0]; i++) {
        if (ends_with(arg, extensions[i])) return true;
    }
    return false;
}

// Decides the route from the visible arguments. A response file (@file) is opaque, so it never changes the route.
static Route classify(int count, char **args) {
    int metal = 0;
    int smith = 0;
    int other = 0;
    for (int i = 0; i < count; i++) {
        const char *arg = args[i];
        if (arg[0] == '@') continue;
        if (arg[0] == '-') {
            if (takes_value(arg) && i + 1 < count) i += 1;
            continue;
        }
        if (ends_with(arg, ".metal")) {
            metal += 1;
        } else if (ends_with(arg, ".smith")) {
            smith += 1;
        } else if (has_other_input_extension(arg)) {
            other += 1;
        }
    }
    if (smith > 0) return Route_Smith;
    if (metal > 0 && other > 0) return Route_Mixed;
    return metal > 0 ? Route_Metal : Route_Clang;
}

// The real path of this executable, so a candidate compiler that is smith under another name can be skipped.
static bool self_path(char *out) {
    char raw[PATH_MAX];
#ifdef __APPLE__
    uint32_t size = sizeof raw;
    if (_NSGetExecutablePath(raw, &size) != 0) return false;
#else
    ssize_t length = readlink("/proc/self/exe", raw, sizeof raw - 1);
    if (length <= 0) return false;
    raw[length] = '\0';
#endif
    return realpath(raw, out) != NULL;
}

static bool is_executable(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode) && access(path, X_OK) == 0;
}

static bool is_not_self(const char *path, const char *self) {
    char real[PATH_MAX];
    return realpath(path, real) != NULL && strcmp(real, self) != 0;
}

static bool is_usable(const char *path, const char *self) {
    return is_executable(path) && is_not_self(path, self);
}

// The first usable executable named tool in a directory of PATH, written to out.
static bool find_in_path(const char *tool, const char *self, char *out) {
    const char *path = getenv("PATH");
    for (const char *start = path; start != NULL && *start != '\0';) {
        const char *end = strchr(start, ':');
        size_t length = end != NULL ? (size_t)(end - start) : strlen(start);
        const char *directory = length > 0 ? start : ".";
        int directory_length = length > 0 ? (int)length : 1;
        int written = snprintf(out, PATH_MAX, "%.*s/%s", directory_length, directory, tool);
        if (written > 0 && written < PATH_MAX && is_usable(out, self)) return true;
        start = end != NULL ? end + 1 : NULL;
    }
    return false;
}

// The compiler for the route: the environment's choice, else Apple's clang on macOS, else Homebrew's LLVM, else PATH.
// Apple's clang is the default there because upstream's AddressSanitizer costs about 40 ms per process on macOS.
static void find_clang(const char *tool, const char *override_name, const char *self, char *out) {
    const char *override = getenv(override_name);
    if (override != NULL && *override != '\0') {
        if (!is_executable(override)) smith_die(smith_exit_missing, "not an executable", override);
        if (!is_not_self(override, self)) smith_die(smith_exit_missing, "refers to smith itself", override);
        snprintf(out, PATH_MAX, "%s", override);
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
        if (written > 0 && written < PATH_MAX && is_usable(out, self)) return;
    }
    if (find_in_path(tool, self, out)) return;
    smith_die(smith_exit_missing, "cannot find a compiler; set SMITH_CLANG or SMITH_CLANGXX, tried", tool);
}

static void show_command(const char *program, char *const *args) {
    fprintf(stderr, "smith: exec %s", program);
    for (size_t i = 1; args[i] != NULL; i++) fprintf(stderr, " %s", args[i]);
    fputc('\n', stderr);
}

static void run(const char *program, char *const *args) {
    if (getenv("SMITH_VERBOSE") != NULL) show_command(program, args);
    execv(program, args);
    smith_die(smith_exit_missing, "cannot execute", program);
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "--smith-version") == 0) {
        puts("smith 0.0 (milestone 0: a routing passthrough)");
        return 0;
    }
    bool which = argc >= 2 && strcmp(argv[1], "--smith-which") == 0;
    int first = which ? 2 : 1;
    int count = argc - first;
    char **args = argv + first;

    char self[PATH_MAX];
    if (!self_path(self)) smith_die(smith_exit_missing, "cannot determine the path of this executable", NULL);

    switch (classify(count, args)) {
    case Route_Smith:
        smith_die(smith_exit_usage, ".smith inputs are not supported yet; milestone 0 is a passthrough", NULL);
    case Route_Mixed:
        smith_die(smith_exit_usage, "cannot compile .metal files together with other inputs in one command; split it",
                  NULL);
    case Route_Metal: {
        static char *metal_argv[smith_metal_args_max + 8];
        char program[PATH_MAX];
        size_t used = 0;
        const char *override = getenv("SMITH_METAL");
        if (override != NULL && *override != '\0') {
            if (!is_executable(override)) smith_die(smith_exit_missing, "not an executable", override);
            snprintf(program, sizeof program, "%s", override);
            metal_argv[used++] = program;
        } else {
            if (!find_in_path("xcrun", self, program)) smith_die(smith_exit_missing, "cannot find xcrun", NULL);
            metal_argv[used++] = program;
            metal_argv[used++] = (char *)"-sdk";
            metal_argv[used++] = (char *)"macosx";
            metal_argv[used++] = (char *)"metal";
        }
        if (which) {
            printf("metal %s", program);
            for (size_t i = 1; i < used; i++) printf(" %s", metal_argv[i]);
            putchar('\n');
            return 0;
        }
        if ((size_t)count + used > smith_metal_args_max) smith_die(smith_exit_usage, "too many arguments", NULL);
        for (int i = 0; i < count; i++) metal_argv[used++] = args[i];
        metal_argv[used] = NULL;
        run(program, metal_argv);
        return smith_exit_missing;
    }
    case Route_Clang: {
        bool plus = ends_with(base_name(argv[0]), "++");
        const char *tool = plus ? "clang++" : "clang";
        char program[PATH_MAX];
        find_clang(tool, plus ? "SMITH_CLANGXX" : "SMITH_CLANG", self, program);
        if (which) {
            printf("clang %s\n", program);
            return 0;
        }
        argv[first - 1] = program; // the full path, so a compiler that finds its own files from argv[0] still can
        run(program, argv + first - 1);
        return smith_exit_missing;
    }
    }
    return smith_exit_missing;
}
