// smith: a drop-in compiler driver.
//
// smith's top level is verbs (smith_verbs, below). `smith cc <args>` and `smith c++ <args>`, and the links `smith-cc`
// and `smith-c++` that act as them by their name, are a pure passthrough: they pick a backend by the kind of input,
// then run that compiler on the arguments, untouched:
//   - a .metal input goes to Apple's Metal compiler: `xcrun -sdk macosx metal`, or $SMITH_METAL if set, which smith
//     replaces itself with;
//   - anything else goes to upstream LLVM's clang. A smith built with SMITH_IN_PROCESS has clang linked in
//     (shim/clang.cpp) and runs clang's own main on the arguments, in its own process, unless $SMITH_CLANG names an
//     external clang to run instead. The stage0, built by one `cc` command, has no clang in it: it replaces itself with
//     an external one, $SMITH_CLANG if set, else Homebrew's llvm (/opt/homebrew/opt/llvm/bin/clang), else the first
//     clang on PATH that is not smith itself. `smith c++`, and the link `smith-c++`, use clang++ ($SMITH_CLANGXX). On
//     macOS it also picks the SDK through the environment, never a flag: see smith_sdk_environment.
// It adds no flags and reads no configuration, so replacing a compiler with smith cannot change a build. See AGENTS.md.
//
// `smith build` compiles the project's build.c with an embedded runtime, runs it to write a ninja manifest, and then
// runs ninja. See docs/build-c.md. `smith fetch <url>` puts a package in the cache and `smith cache` shows and cleans
// the caches. `smith new` writes a ready project: build.c, main.c, the rules and the plugin settings.
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#ifdef SMITH_IN_PROCESS
#include "scripts/pack.h"

#include <compression.h>
#include <dirent.h>
#include <ftw.h>
#include <inttypes.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <sys/time.h>
#include <time.h>

// SMITH_CLANG_ID is the version of the clang linked in and a hash of what is linked (scripts/llvm.sh), compiled in by
// build.c; --smith-which prints it, so that what depends on the compiler is rebuilt for a new LLVM and for nothing
// else. shim/clang.cpp, the only C++ in smith: clang's own main, run as `program` (a clang or clang++ in the folder
// that smith_resource_ensure fills with clang's resource directory) on argv[1 .. argc). argv[0] is ignored.
int smith_clang_main(const char *program, int argc, char **argv, bool lld, const char *resource);
#ifdef SMITH_PROVE
// shim/prove.cpp: `smith prove`, on argv[0 .. argc), compiling with the clang that smith_clang_main runs as `program`
// (with `resource` as its resource directory).
int smith_prove_main(const char *program, const char *resource, int argc, char **argv);
#endif
// shim/lld.cpp: lld's Mach-O linker, run as ld on argv[1 .. argc). Exits the process when it is done.
int smith_lld_main(int argc, char **argv);
// shim/format.cpp, shim/tidy.cpp and shim/clangd.cpp: the mains of clang-format, clang-tidy and clangd, each run on
// argv[1 .. argc), as argv[0], a path in this smith's programs folder (smith_programs_folder, beside clang's), whose
// resource directory clang-tidy and clangd use.
int smith_format_main(int argc, char **argv);
int smith_tidy_main(int argc, char **argv);
int smith_clangd_main(int argc, char **argv);
#ifdef SMITH_LLDB
constexpr bool smith_has_lldb = true;
// shim/lldb.cpp and shim/lldb_dap.cpp: the mains of lldb and lldb-dap (`smith dbg`, `smith dap`), run the same way. A
// smith built against an LLVM install without lldb (scripts/llvm.sh, the lldb step) runs an external lldb instead.
int smith_lldb_main(int argc, char **argv);
int smith_lldb_dap_main(int argc, char **argv);
#else
constexpr bool smith_has_lldb = false;
#endif
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
    static const char *const extensions[] = {".c", ".h", ".m", ".mm", ".cc", ".cpp", ".cxx",   ".C",    ".o",
                                             ".a", ".s", ".S", ".i",  ".ii", ".pcm", ".dylib", ".smith"};
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
// What the driver looks up beside its program, carried inside smith.
//
// The driver finds its builtin headers (lib/clang/<major>/include), the compiler-rt libraries (lib/clang/<major>/lib),
// the sanitizers' ignore lists (lib/clang/<major>/share) and the libLTO.dylib it hands the linker (lib/libLTO.dylib)
// from the path it is run as: `<folder>/bin/clang` has them in `<folder>/lib`. So smith embeds exactly those files,
// packed and compressed by scripts/pack.c from the LLVM install (the macOS runtimes only, not iOS's), and writes them
// once, on first use, to a folder named for the pack's contents, then runs the driver as that folder's `bin/clang`.
// Nothing else is read from the install. The linker, dsymutil and the other tools are the system's, found on PATH.
//
// The folder is outside the caches smith prunes and cleans, in the user's data folder, and is never removed by smith:
// a sanitized program records an absolute rpath to the runtime's `lib/darwin` in it, and must keep loading it after
// `smith cache clean` and after the 31 days. A new LLVM or other contents make a new folder; the old one stays, for the
// programs that were built against it.
// The pack is smith_clang_resource (runtime/pack_data.c).
static unsigned char smith_resource_chunk[smith_pack_chunk_max];
static unsigned char smith_resource_scratch[1U << 22]; // for libcompression: LZFSE needs a fraction of it

static SmithPackHeader smith_resource_header(void) {
    SmithPackHeader header;
    SMITH_ASSERT(smith_clang_resource_size >= sizeof header);
    memcpy(&header, smith_clang_resource, sizeof header);
    SMITH_ASSERT(memcmp(header.magic, "SMITHPK1", 8) == 0 && header.count > 0 && header.count <= smith_pack_files_max);
    return header;
}

// <data>/smith/clang/llvm-<version>-<key>, where <data> is $XDG_DATA_HOME or $HOME/.local/share.
static void smith_resource_folder(char *folder) {
    const char *data = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    char base[PATH_MAX];
    if (data != nullptr && data[0] == '/') {
        snprintf(base, sizeof base, "%s", data);
    } else if (home != nullptr && home[0] == '/') {
        snprintf(base, sizeof base, "%s/.local/share", home);
    } else {
        smith_die(smith_exit_usage, "cannot find a folder for clang's files: set HOME or XDG_DATA_HOME", nullptr);
    }
    // The name is the pack's hash carried on over the compiler's identity (FNV-1a, as scripts/pack.c), so another
    // contents or another compiler is another folder.
    uint64_t key = smith_resource_header().hash;
    for (const char *c = SMITH_CLANG_ID; *c != '\0'; c++) // the identity is a short constant
        key = (key ^ (unsigned char)*c) * 1099511628211ULL;
    size_t version = strcspn(SMITH_CLANG_ID, " ");
    int written =
        snprintf(folder, PATH_MAX, "%s/smith/clang/%.*s-%016" PRIx64, base, (int)version, SMITH_CLANG_ID, key);
    if (written <= 0 || written >= PATH_MAX - 64)
        smith_die(smith_exit_usage, "the data folder's path is too long", base);
}

// mkdir -p for the folders above `path`'s last component (all of them when `whole`).
static void smith_make_folders(const char *path, bool whole) {
    char partial[PATH_MAX];
    size_t length = strlen(path);
    SMITH_ASSERT(length > 0 && length < sizeof partial);
    memcpy(partial, path, length + 1);
    for (size_t i = 1; i <= length; i++) { // bounded by length
        if (partial[i] != '/' && !(whole && partial[i] == '\0')) continue;
        partial[i] = '\0';
        if (mkdir(partial, 0755) != 0 && errno != EEXIST) smith_die(smith_exit_usage, "cannot create", partial);
        if (i < length) partial[i] = '/';
    }
}

static int smith_remove_entry(const char *path, const struct stat *info, int kind, struct FTW *where) {
    (void)info;
    (void)kind;
    (void)where;
    return remove(path);
}

static void smith_remove_tree(const char *path) {
    nftw(path, smith_remove_entry, 64, FTW_DEPTH | FTW_PHYS);
}

// Writes one file of the pack under `root` and returns the pack's offset after it.
static size_t smith_resource_write(const char *root, size_t offset) {
    SmithPackFile file;
    SMITH_ASSERT(offset + sizeof file <= smith_clang_resource_size);
    memcpy(&file, smith_clang_resource + offset, sizeof file);
    offset += sizeof file;
    SMITH_ASSERT(file.path_length > 0 && file.path_length < smith_pack_path_max && file.size <= smith_pack_chunk_max);
    SMITH_ASSERT(offset + file.path_length + file.stored <= smith_clang_resource_size && file.stored <= file.size);
    char path[PATH_MAX];
    int written = snprintf(path, sizeof path, "%s/%.*s", root, (int)file.path_length,
                           (const char *)smith_clang_resource + offset);
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof path);
    SMITH_ASSERT(strstr(path, "/../") == nullptr);
    offset += file.path_length;
    const unsigned char *data = smith_clang_resource + offset;
    if (file.stored == file.size) {
        memcpy(smith_resource_chunk, data, file.size);
    } else {
        SMITH_ASSERT(compression_decode_scratch_buffer_size(COMPRESSION_LZFSE) <= sizeof smith_resource_scratch);
        size_t got = compression_decode_buffer(smith_resource_chunk, sizeof smith_resource_chunk, data, file.stored,
                                               smith_resource_scratch, COMPRESSION_LZFSE);
        SMITH_ASSERT(got == file.size);
    }
    smith_make_folders(path, false);
    int fd = open(path, file.continued ? O_WRONLY | O_APPEND : O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) smith_die(smith_exit_usage, "cannot create", path);
    for (size_t done = 0; done < file.size;) { // each pass writes at least one byte, or stops
        ssize_t put = write(fd, smith_resource_chunk + done, file.size - done);
        if (put <= 0) smith_die(smith_exit_usage, "cannot write", path);
        done += (size_t)put;
    }
    if (fchmod(fd, (mode_t)file.mode) != 0 || close(fd) != 0) smith_die(smith_exit_usage, "cannot write", path);
    return offset + file.stored;
}

static bool smith_is_folder(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

// Writes the pack's files under `folder`. They are written beside it under a name of this process and renamed into
// place, so a folder that exists is whole, and processes that start together (a ninja run) all find one, whichever
// wins.
static void smith_resource_unpack(const char *folder) {
    char temporary[PATH_MAX];
    int written = snprintf(temporary, sizeof temporary, "%s.%d.tmp", folder, (int)getpid());
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof temporary);
    smith_remove_tree(temporary);
    smith_make_folders(temporary, true);
    SmithPackHeader header = smith_resource_header();
    size_t offset = sizeof header;
    for (uint32_t i = 0; i < header.count; i++) // bounded by the count, which smith_resource_header limits
        offset = smith_resource_write(temporary, offset);
    SMITH_ASSERT(offset == smith_clang_resource_size);
    if (rename(temporary, folder) != 0) { // another process may have won
        bool won = smith_is_folder(folder);
        smith_remove_tree(temporary);
        if (!won) smith_die(smith_exit_usage, "cannot create", folder);
    }
}

// The programs the driver runs, apart from the files it reads. The runtime folder above is named for the contents and
// the compiler's identity, and never moves, because a sanitized binary records a path into it. The programs are hard
// links to the smith that is running, and many different smiths (the installed one, a build of a branch, another
// agent's) share one runtime folder, so they live in a folder named for the smith itself, its LC_UUID:
// <data>/smith/programs/<uuid>/{bin/clang, bin/clang++, bin/ld, lib -> the runtime folder's lib}. Two smiths never
// share or remake each other's links, and a smith without lld has no `ld` to be found. The driver runs `bin/clang`
// again with -cc1 for a compile with more than one step, so it must be smith, which runs -cc1 in process; a hard link
// keeps working when the smith it was made from is replaced or removed, as an install does (a symbolic link across
// volumes). The folder is built beside its place and renamed into it, so one that exists is whole.
//
// A folder not used for 31 days is removed when a new one is made (using one touches it once a day), and so is a half
// built one older than an hour. Only the runtime folder must live forever.
constexpr time_t smith_programs_idle_seconds = 31L * 24 * 3600;
constexpr time_t smith_programs_touch_seconds = 24L * 3600;
constexpr time_t smith_programs_temporary_seconds = 3600;

// The hex of the running executable's LC_UUID, which the linker derives from its contents: the same smith is the same
// name, another build another.
static void smith_self_uuid(char *out, size_t size) {
    SMITH_ASSERT(size >= 33);
    const struct mach_header_64 *header = (const struct mach_header_64 *)_dyld_get_image_header(0);
    SMITH_ASSERT(header != nullptr && header->magic == MH_MAGIC_64);
    const struct load_command *command = (const struct load_command *)(header + 1);
    for (uint32_t i = 0; i < header->ncmds; i++) { // bounded by the header
        if (command->cmd == LC_UUID) {
            const struct uuid_command *uuid = (const struct uuid_command *)command;
            for (size_t k = 0; k < sizeof uuid->uuid; k++)
                snprintf(out + 2 * k, 3, "%02x", uuid->uuid[k]);
            return;
        }
        command = (const struct load_command *)((const char *)command + command->cmdsize);
    }
    smith_die(smith_exit_usage, "this smith has no LC_UUID", nullptr);
}

// <data>/smith/programs/<uuid>, from the runtime folder <data>/smith/clang/<name>.
static void smith_programs_folder(const char *runtime, char *programs) {
    char uuid[33];
    smith_self_uuid(uuid, sizeof uuid);
    const char *clang = strstr(runtime, "/smith/clang/");
    SMITH_ASSERT(clang != nullptr);
    int written = snprintf(programs, PATH_MAX, "%.*s/smith/programs/%s", (int)(clang - runtime), runtime, uuid);
    SMITH_ASSERT(written > 0 && written < PATH_MAX - 32);
}

// Removes what the folder `parent` holds that is idle for too long, but `keep`.
static void smith_programs_prune(const char *parent, const char *keep) {
    DIR *directory = opendir(parent);
    if (directory == nullptr) return;
    time_t now = time(nullptr);
    for (struct dirent *entry = readdir(directory); entry != nullptr; entry = readdir(directory)) { // the folder's size
        if (entry->d_name[0] == '.') continue;
        char path[PATH_MAX];
        int written = snprintf(path, sizeof path, "%s/%s", parent, entry->d_name);
        if (written <= 0 || written >= (int)sizeof path || strcmp(path, keep) == 0) continue;
        struct stat info;
        if (lstat(path, &info) != 0) continue;
        bool temporary = smith_ends_with(entry->d_name, ".tmp");
        time_t idle = now - info.st_mtime;
        if (idle > (temporary ? smith_programs_temporary_seconds : smith_programs_idle_seconds))
            smith_remove_tree(path);
    }
    closedir(directory);
}

// Removes the half-made links an older smith left in the runtime folder's bin (it made them there, and raced). Its
// finished links stay, for that smith.
static void smith_programs_sweep_legacy(const char *runtime) {
    char bin[PATH_MAX];
    snprintf(bin, sizeof bin, "%s/bin", runtime);
    DIR *directory = opendir(bin);
    if (directory == nullptr) return;
    time_t now = time(nullptr);
    for (struct dirent *entry = readdir(directory); entry != nullptr; entry = readdir(directory)) { // the folder's size
        char path[PATH_MAX];
        int written = snprintf(path, sizeof path, "%s/%s", bin, entry->d_name);
        struct stat info;
        if (written <= 0 || written >= (int)sizeof path || !smith_ends_with(entry->d_name, ".tmp")) continue;
        if (lstat(path, &info) == 0 && now - info.st_mtime > smith_programs_temporary_seconds) unlink(path);
    }
    closedir(directory);
}

static void smith_programs_make(const char *runtime, const char *programs, const char *self) {
    char temporary[PATH_MAX];
    int written = snprintf(temporary, sizeof temporary, "%s.%d.tmp", programs, (int)getpid());
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof temporary - 32);
    smith_remove_tree(temporary);
    smith_make_folders(temporary, true);
    static const char *const names[] = {"clang", "clang++", "ld", "lldb-argdumper"};
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/bin", temporary);
    if (mkdir(path, 0755) != 0) smith_die(smith_exit_usage, "cannot create", path);
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        snprintf(path, sizeof path, "%s/bin/%s", temporary, names[i]);
        if (link(self, path) != 0 && symlink(self, path) != 0) smith_die(smith_exit_usage, "cannot link", path);
    }
    char target[PATH_MAX];
    snprintf(target, sizeof target, "%s/lib", runtime);
    snprintf(path, sizeof path, "%s/lib", temporary);
    if (symlink(target, path) != 0) smith_die(smith_exit_usage, "cannot link", path);
    if (rename(temporary, programs) != 0) { // another process of this smith may have won
        bool won = smith_is_folder(programs);
        smith_remove_tree(temporary);
        if (!won) smith_die(smith_exit_usage, "cannot create", programs);
    }
}

// Makes the runtime folder and this smith's programs exist; a second call, or a second process, finds them.
static void smith_resource_ensure(const char *runtime, const char *programs, const char *self) {
    if (!smith_is_folder(runtime)) smith_resource_unpack(runtime);
    struct stat info;
    if (stat(programs, &info) == 0 && S_ISDIR(info.st_mode)) {
        if (time(nullptr) - info.st_mtime > smith_programs_touch_seconds) utimes(programs, nullptr);
        return;
    }
    smith_programs_make(runtime, programs, self);
    char parent[PATH_MAX];
    snprintf(parent, sizeof parent, "%s", programs);
    *strrchr(parent, '/') = '\0';
    smith_programs_prune(parent, programs);
    smith_programs_sweep_legacy(runtime);
}

// The compiler's resource directory in the runtime folder: lib/clang/<major>, the one folder in lib/clang.
static void smith_resource_directory(const char *runtime, char *out) {
    char parent[PATH_MAX];
    snprintf(parent, sizeof parent, "%s/lib/clang", runtime);
    DIR *directory = opendir(parent);
    SMITH_ASSERT(directory != nullptr);
    out[0] = '\0';
    for (struct dirent *entry = readdir(directory); entry != nullptr && out[0] == '\0'; entry = readdir(directory)) {
        if (entry->d_name[0] == '.') continue;
        int written = snprintf(out, PATH_MAX, "%s/%s", parent, entry->d_name);
        SMITH_ASSERT(written > 0 && written < PATH_MAX);
    }
    closedir(directory);
    SMITH_ASSERT(out[0] != '\0');
}

// smith run as `ld`, which is what <folder>/bin/ld is: the driver looks for its linker beside its own program before
// PATH, so the link it starts is this smith, and lld links in this process. $SMITH_LINKER names a linker to run instead
// (the system's /usr/bin/ld, to compare, or as a way out), and gets the driver's arguments as they are.
static bool smith_has_linker_override(void) {
    const char *override = getenv("SMITH_LINKER");
    return override != nullptr && *override != '\0';
}

// smith run as `lldb-argdumper` (a fourth link in the programs folder): lldb runs it through the shell to expand the
// arguments of a program it starts, and reads back {"arguments": [...]}, as LLVM's lldb-argdumper prints it.
static int smith_argdumper(int argc, char **argv) {
    fputs("{\"arguments\":[", stdout);
    for (int i = 1; i < argc; i++) {
        if (i > 1) putchar(',');
        putchar('"');
        for (const char *c = argv[i]; *c != '\0'; c++) {
            if (*c == '"' || *c == '\\') putchar('\\');
            if ((unsigned char)*c < 0x20) {
                printf("\\u%04x", (unsigned)*c);
            } else {
                putchar(*c);
            }
        }
        putchar('"');
    }
    fputs("]}", stdout);
    return 0;
}

static int smith_route_linker(int argc, char **argv) {
    const char *override = getenv("SMITH_LINKER");
    if (smith_has_linker_override()) {
        argv[0] = (char *) override;
        execv(override, argv);
        smith_die(smith_exit_missing, "cannot run the linker named by SMITH_LINKER", override);
    }
    return smith_lld_main(argc, argv);
}

// Whether clang runs in this process: unless the caller's variable names an external clang to run instead.
static bool smith_runs_in_process(const char *override_name) {
    const char *override = getenv(override_name);
    return override == nullptr || *override == '\0';
}

static int smith_route_in_process(int argc, char **argv, int first, bool which, bool plus, const char *self) {
    char folder[PATH_MAX];
    char programs[PATH_MAX];
    char program[PATH_MAX];
    smith_resource_folder(folder);
    smith_programs_folder(folder, programs);
    int written = snprintf(program, sizeof program, "%s/bin/%s", programs, plus ? "clang++" : "clang");
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof program); // smith_programs_folder leaves room for it
    if (which) { // names the program but writes nothing: its folder is made by the first compile
#ifdef SMITH_HOOK
        printf("clang in-process %s\nidentity %s\ntoken-hook yes\n", program, SMITH_CLANG_ID);
#else
        printf("clang in-process %s\nidentity %s\ntoken-hook no\n", program, SMITH_CLANG_ID);
#endif
        return 0;
    }
    smith_resource_ensure(folder, programs, self);
    char resource[PATH_MAX];
    smith_resource_directory(folder, resource);
#ifdef __APPLE__
    smith_sdk_environment(self);
#endif
    if (getenv("SMITH_VERBOSE") != nullptr) smith_show_command("in-process", program, argv + first - 1);
    return smith_clang_main(program, argc - first + 1, argv + first - 1, !smith_has_linker_override(), resource);
}
#endif

constexpr size_t smith_smith_args_max = 8192; // arguments of a command with .smith inputs

// A .smith file is C to the compiler, so each is given with `-x c` and the language is reset after it: `x.smith y.c`
// becomes
// `-x c x.smith -x none y.c`. The compiler's hook (shim/hook.cpp), which the .smith extension turns on, does the rest.
// Nothing is changed when the command names a language itself (-x), or is clang's own `-cc1` form, where the driver
// has done this already. Returns the new argument count, or 0 when the command stays as it is.
static int smith_rewrite_smith_inputs(int argc, char **argv, int first, char **out) {
    bool found = false;
    for (int i = first; i < argc; i++) { // bounded by argc
        if (strcmp(argv[i], "-cc1") == 0 || strncmp(argv[i], "-x", 2) == 0) return 0;
        if (argv[i][0] == '-' && smith_takes_value(argv[i]) && i + 1 < argc) {
            i += 1;
        } else if (argv[i][0] != '-' && smith_ends_with(argv[i], ".smith")) {
            found = true;
        }
    }
    if (!found) return 0;
    size_t used = 0;
    bool reset = false; // a .smith input was given, and the language is still C
    for (int i = 0; i < first; i++)
        out[used++] = argv[i];
    for (int i = first; i < argc; i++) { // bounded by argc
        if (used + 6 > smith_smith_args_max)
            smith_die(smith_exit_usage, "too many arguments with .smith inputs", nullptr);
        bool value = argv[i][0] == '-' && smith_takes_value(argv[i]) && i + 1 < argc;
        bool smith_input = argv[i][0] != '-' && smith_ends_with(argv[i], ".smith");
        if (value) {
            out[used++] = argv[i];
            out[used++] = argv[++i];
        } else if (smith_input) {
            out[used++] = (char *)"-x";
            out[used++] = (char *)"c";
            out[used++] = argv[i];
            reset = true;
        } else {
            if (reset && argv[i][0] != '-' && argv[i][0] != '@') {
                out[used++] = (char *)"-x";
                out[used++] = (char *)"none";
                reset = false;
            }
            out[used++] = argv[i];
        }
    }
    out[used] = nullptr;
    return (int)used;
}

static int smith_route_clang(int argc, char **argv, int first, bool which, bool plus, const char *self) {
    SMITH_ASSERT(first >= 1 && argc >= first);
    char *rewritten[smith_smith_args_max + 1];
    int rewritten_count = smith_rewrite_smith_inputs(argc, argv, first, rewritten);
#ifndef SMITH_HOOK
    if (rewritten_count > 0) {
        smith_die(smith_exit_usage,
                  ".smith files need a smith whose clang has the token hook (patches/clang-token-hook.patch, applied "
                  "by `smith build llvm`); this one has none",
                  nullptr);
    }
#endif
    if (rewritten_count > 0) {
        argc = rewritten_count;
        argv = rewritten;
    }
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

// smith prove: the prover (Alive2, Bitwuzla and Z3) is linked into every full smith, which `smith build llvm` makes
// possible (build.c); only the stage0 says it has none.
static int smith_prove(int argc, char **argv, const char *self) {
#if defined(SMITH_PROVE) && defined(SMITH_IN_PROCESS)
    char folder[PATH_MAX];
    char programs[PATH_MAX];
    char program[PATH_MAX];
    smith_resource_folder(folder);
    smith_programs_folder(folder, programs);
    int written = snprintf(program, sizeof program, "%s/bin/clang", programs);
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof program); // smith_programs_folder leaves room for it
    smith_resource_ensure(folder, programs, self);
    char resource[PATH_MAX];
    smith_resource_directory(folder, resource);
#ifdef __APPLE__
    smith_sdk_environment(self);
#endif
    return smith_prove_main(program, resource, argc, argv);
#else
    (void)argc;
    (void)argv;
    (void)self;
    smith_die(smith_exit_usage, "this smith is the stage0, which has no prover; the full smith has it",
              "smith build llvm, then smith build");
#endif
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
static const unsigned char smith_build_gzip[] = {
#embed "runtime/gzip.c"
};
static const unsigned char smith_build_xz[] = {
#embed "runtime/xz.c"
};
static const unsigned char smith_build_untar[] = {
#embed "runtime/untar.c"
};

// smith's libraries: each header and source is written into the project's .smith/ on every `smith build`, where build.c
// finds the source with smith_library and the compiles find the header through -I .smith/include.
static const unsigned char smith_arena_header[] = {
#embed "include/smith/arena.h"
};
static const unsigned char smith_arena_source[] = {
#embed "lib/arena.c"
};

static const unsigned char smith_contract_header[] = {
#embed "include/smith/contract.h"
};
static const unsigned char smith_contract_source[] = {
#embed "lib/contract.c"
};

static const unsigned char smith_io_header[] = {
#embed "include/smith/io.h"
};
static const unsigned char smith_io_source[] = {
#embed "lib/io.c"
};

static const unsigned char smith_json_header[] = {
#embed "include/smith/json.h"
};
static const unsigned char smith_json_source[] = {
#embed "lib/json.c"
};

static const unsigned char smith_random_header[] = {
#embed "include/smith/random.h"
};
static const unsigned char smith_random_source[] = {
#embed "lib/random.c"
};

static const unsigned char smith_text_header[] = {
#embed "include/smith/text.h"
};
static const unsigned char smith_text_source[] = {
#embed "lib/text.c"
};
// The HTTPS client is smith's own for now, not a library a build.c can ask for: it goes to .smith/include/smith/ and
// .smith/internal/, where the runtime's package.c finds it, and not to .smith/lib/ (which smith_library lists).
static const unsigned char smith_embedded_http_header[] = {
#embed "include/smith/http.h"
};
static const unsigned char smith_embedded_http_source[] = {
#embed "lib/http.c"
};

static const unsigned char smith_test_header[] = {
#embed "include/smith/test.h"
};
static const unsigned char smith_test_source[] = {
#embed "lib/test.c"
};

static const unsigned char smith_trace_header[] = {
#embed "include/smith/trace.h"
};
static const unsigned char smith_trace_source[] = {
#embed "lib/trace.c"
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
    {"contract", smith_contract_header, sizeof smith_contract_header, smith_contract_source,
     sizeof smith_contract_source},
    {"io", smith_io_header, sizeof smith_io_header, smith_io_source, sizeof smith_io_source},
    {"json", smith_json_header, sizeof smith_json_header, smith_json_source, sizeof smith_json_source},
    {"random", smith_random_header, sizeof smith_random_header, smith_random_source, sizeof smith_random_source},
    {"test", smith_test_header, sizeof smith_test_header, smith_test_source, sizeof smith_test_source},
    {"text", smith_text_header, sizeof smith_text_header, smith_text_source, sizeof smith_text_source},
    {"trace", smith_trace_header, sizeof smith_trace_header, smith_trace_source, sizeof smith_trace_source},
};

// `smith symbolize` and smith as llvm-symbolizer: stack traces turned into lines.
#include "runtime/symbolize.c"
// Packages, shared with the build runtime: the cache, the download and SHA-256.
#include "runtime/package.c"
// `smith cache`, and the lock a build holds on the caches.
#include "runtime/cache.c"
// The executor that runs the build graph, in place of ninja.
#include "runtime/executor.c"
// The lowering of .smith files, and the header pass that needs no compiler.
#include "runtime/lang.c"
#include "runtime/lang_lex.c"

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

// The manifest that compiles build.c into .smith/program with the clang `smith` would pick. When that is smith itself
// (in_process) the program is `<smith> cc`, since smith's top level is verbs.
static size_t smith_bootstrap_manifest(char *out, size_t capacity, const char *clang, bool in_process) {
    char word[PATH_MAX * 2];
    char quoted[PATH_MAX * 2 + 4];
    smith_quote_word(word, sizeof word, clang);
    int made = snprintf(quoted, sizeof quoted, "%s%s", word, in_process ? " cc" : "");
    SMITH_ASSERT(made > 0 && (size_t)made < sizeof quoted);
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

static void smith_build_prepare(const char *clang, bool in_process) {
    smith_make_folder(".smith");
    smith_make_folder(".smith/include");
    smith_make_folder(".smith/include/smith");
    smith_make_folder(".smith/lib");
    smith_make_folder(".smith/internal");
    smith_make_folder(".smith/bootstrap");
    static char manifest[smith_build_manifest_max];
    size_t length = smith_bootstrap_manifest(manifest, sizeof manifest, clang, in_process);
    // Git ignores smith's folder with no change to the project, as pytest, ruff and mypy do for their caches.
    smith_write_if_changed(".smith/.gitignore", (const unsigned char *)"*\n", 2);
    smith_write_if_changed(".smith/include/smith/build.h", smith_build_header, sizeof smith_build_header);
    smith_build_write_libraries();
    smith_write_if_changed(".smith/include/smith/http.h", smith_embedded_http_header,
                           sizeof smith_embedded_http_header);
    smith_write_if_changed(".smith/internal/http.c", smith_embedded_http_source, sizeof smith_embedded_http_source);
    smith_write_if_changed(".smith/runtime.c", smith_build_runtime, sizeof smith_build_runtime);
    smith_write_if_changed(".smith/package.c", smith_build_package, sizeof smith_build_package);
    smith_write_if_changed(".smith/gzip.c", smith_build_gzip, sizeof smith_build_gzip);
    smith_write_if_changed(".smith/xz.c", smith_build_xz, sizeof smith_build_xz);
    smith_write_if_changed(".smith/untar.c", smith_build_untar, sizeof smith_build_untar);
    smith_write_if_changed(".smith/bootstrap/build.ninja", (const unsigned char *)manifest, length);
}

// What compiles and links build.c: an external clang, found as for any compile, or smith itself when clang is in it
// (then true, and the commands are `smith cc ...`).
static bool smith_build_compiler(const char *self, char *out) {
#ifdef SMITH_IN_PROCESS
    if (smith_runs_in_process("SMITH_CLANG")) {
        int written = snprintf(out, PATH_MAX, "%s", self);
        SMITH_ASSERT(written > 0 && written < PATH_MAX); // self came from a PATH_MAX buffer
        return true;
    }
#endif
    smith_find_clang("clang", "SMITH_CLANG", self, out);
    return false;
}

// The flags of ninja that `smith build` takes, for the executor: -j, -k, -n, -v, --quiet and -t. -l, -d and -w are read
// and ignored. Anything else that starts with a dash is an error; the rest are the targets.
constexpr int smith_build_jobs_max = (int)smith_exec_jobs_max;

static int smith_build_default_jobs(void) {
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    int jobs = cpus <= 1 ? 2 : cpus == 2 ? 3 : (int)cpus + 2; // ninja's own default
    return jobs > smith_build_jobs_max ? smith_build_jobs_max : jobs;
}

static int smith_build_number(const char *flag, const char *text) {
    char *end = nullptr;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (*text == '\0' || *end != '\0' || errno != 0 || value < 0 || value > INT_MAX)
        smith_die(smith_exit_usage, "needs a number", flag);
    return (int)value;
}

// The value of a flag that takes one, written "-j8" or "-j 8".
static const char *smith_build_flag_value(int count, char **args, int *i) {
    const char *arg = args[*i];
    if (arg[2] != '\0') return arg + 2;
    if (*i + 1 >= count) smith_die(smith_exit_usage, "needs a value", arg);
    *i += 1;
    return args[*i];
}

static void smith_build_flag(int count, char **args, int *i, SmithExecOptions *options) {
    const char *arg = args[*i];
    if (strcmp(arg, "-n") == 0) {
        options->dry_run = true;
    } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
        options->verbose = true;
    } else if (strcmp(arg, "--quiet") == 0) {
        options->quiet = true;
    } else if (strncmp(arg, "-j", 2) == 0) {
        int jobs = smith_build_number("-j", smith_build_flag_value(count, args, i));
        options->jobs = jobs == 0                     ? smith_build_default_jobs()
                        : jobs > smith_build_jobs_max ? smith_build_jobs_max
                                                      : jobs;
    } else if (strncmp(arg, "-k", 2) == 0) {
        int allowed = smith_build_number("-k", smith_build_flag_value(count, args, i));
        options->failures_allowed = allowed == 0 ? INT_MAX : allowed;
    } else if (strncmp(arg, "-d", 2) == 0) {
        options->explain = options->explain || strcmp(smith_build_flag_value(count, args, i), "explain") == 0;
    } else if (strncmp(arg, "-l", 2) == 0 || strncmp(arg, "-w", 2) == 0) {
        (void)smith_build_flag_value(count, args, i);
    } else {
        smith_die(smith_exit_usage, "unknown option", arg);
    }
}

static SmithExecOptions smith_build_options(int count, char **args) {
    static const char *targets[smith_build_args_max];
    SmithExecOptions options = {.jobs = smith_build_default_jobs(), .failures_allowed = 1, .targets = targets};
    for (int i = 0; i < count; i++) { // bounded by count, which main limits
        if (strcmp(args[i], "-t") == 0) {
            if (i + 1 >= count) smith_die(smith_exit_usage, "-t needs a tool", nullptr);
            options.tool = args[i + 1];
            options.tool_args = (const char *const *)(args + i + 2);
            options.tool_arg_count = (size_t)(count - i - 2);
            break;
        }
        if (args[i][0] == '-' && args[i][1] != '\0')
            smith_build_flag(count, args, &i, &options);
        else
            targets[options.target_count++] = args[i];
    }
    return options;
}

// SMITH_NINJA=<ninja> runs the manifest with that ninja instead of the executor: the manifest is the same.
static int smith_build_with_ninja(char *ninja, char **ninja_argv, char **program_argv) {
    char *bootstrap_argv[] = {ninja, (char *)"--quiet", (char *)"-f", (char *)".smith/bootstrap/build.ninja", nullptr};
    int status = smith_build_spawn(ninja, bootstrap_argv);
    if (status == 0) status = smith_build_spawn(program_argv[0], program_argv);
    if (status != 0) return status;
    smith_run(ninja, ninja_argv);
    return smith_exit_missing;
}

static bool smith_is_project_verb(const char *name);

// `smith build run` is `smith run`: a project verb is a verb, so it has one way to be run. Anything after -t is the
// tool's own arguments.
static void smith_build_refuse_verbs(int count, char **args) {
    for (int i = 0; i < count; i++) { // bounded by count, which smith_build_main limits
        if (strcmp(args[i], "-t") == 0) return;
        if (args[i][0] == '-' || !smith_is_project_verb(args[i])) continue;
        fprintf(stderr, "smith: %s is a verb, not a target of smith build; run `smith %s`\n", args[i], args[i]);
        exit(smith_exit_usage);
    }
}

// verb is nullptr for `smith build`, and the project verb that runs this build (smith_project_main) otherwise.
static int smith_build_main(int count, char **args, const char *self, const char *verb) {
    SMITH_ASSERT(count >= 0);
    if ((size_t)count <= smith_build_args_max && verb == nullptr) smith_build_refuse_verbs(count, args);
    if (access("build.c", R_OK) != 0) smith_die(smith_exit_usage, "no build.c in this folder", nullptr);
    smith_cache_hold();
    setenv("SMITH_SELF", self, 1); // the build program finds smith here: `smith symbolize` for the tests' failures
    const char *forced = getenv("SMITH_NINJA");
    bool use_ninja = forced != nullptr && *forced != '\0';
    char ninja[PATH_MAX] = "";
    if (use_ninja) smith_build_find_ninja(self, ninja);
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
    SmithExecOptions options =
        smith_build_options((int)(ninja_used - smith_build_ninja_prefix), ninja_argv + smith_build_ninja_prefix);
    options.verb = verb;

    char clang[PATH_MAX];
    bool in_process = smith_build_compiler(self, clang);
#ifdef __APPLE__
    smith_sdk_environment(self); // the compiles inherit it, as do the programs build.c runs
#endif
    smith_build_prepare(clang, in_process);
    if (use_ninja) return smith_build_with_ninja(ninja, ninja_argv, program_argv);
    SmithIo io = smith_io_make(0, nullptr);
    SmithExecOptions bootstrap = {.jobs = options.jobs, .failures_allowed = 1, .quiet = true};
    int status = smith_exec_run(&io, ".smith/bootstrap/build.ninja", &bootstrap);
    if (status == 0) status = smith_build_spawn(program_argv[0], program_argv);
    if (status != 0) return status;
    return smith_exec_run(&io, ".smith/build.ninja", &options);
}

// `smith --smith-exec <manifest> [flags and targets]` runs any manifest the executor reads, as ninja would, in the
// current folder. The tests use it to compare the executor with ninja on generated graphs.
static int smith_exec_main(int count, char **args) {
    if (count < 1) smith_die(smith_exit_usage, "usage: smith --smith-exec <manifest> [flags and targets]", nullptr);
    SmithExecOptions options = smith_build_options(count - 1, args + 1);
    SmithIo io = smith_io_make(0, nullptr);
    return smith_exec_run(&io, args[0], &options);
}

// smith fetch.

// `smith fetch <url>` downloads an archive into the package cache and prints the line to paste into build.c.
static int smith_fetch_main(int count, char **args) {
    if (count != 1) smith_die(smith_exit_usage, "usage: smith fetch <url>", nullptr);
    char folder[PATH_MAX];
    char hash[smith_package_hash_size];
    SmithPackageError error;
    SmithIo io = smith_io_make(0, nullptr);
    bool fetched = smith_package_ensure(&io, args[0], nullptr, false, folder, hash, &error);
    smith_io_close(&io);
    if (!fetched) smith_die(smith_exit_usage, "fetch failed:", error.text);
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
static const unsigned char smith_new_agents[] = {
#embed "templates/new/agents.md"
};
static const unsigned char smith_new_knowledge_index[] = {
#embed "templates/new/knowledge-index.md"
};
static const unsigned char smith_new_knowledge_program[] = {
#embed "templates/new/knowledge-program.md"
};
static const unsigned char smith_new_docs_skill[] = {
#embed "templates/new/docs-skill.md"
};

typedef struct {
    const char *path;
    const unsigned char *bytes;
    size_t size;
    bool named;       // @NAME@ is replaced by the program's name, and @KEBAB@ by the same in kebab-case
    bool about_main;  // describes main.c, so a project that keeps its own sources does not get it
    bool lists_main;  // its last line lists what describes main.c, which that project does not get either
    const char *link; // not a file: a symbolic link to this
} SmithNewFile;

static const SmithNewFile smith_new_files[] = {
    {"build.c", smith_new_build_c, sizeof smith_new_build_c, true, false, false, nullptr},
    {"main.c", smith_new_main_c, sizeof smith_new_main_c, true, false, false, nullptr},
    {".gitignore", smith_new_gitignore, sizeof smith_new_gitignore, false, false, false, nullptr},
    {".clang-format", smith_new_clang_format, sizeof smith_new_clang_format, false, false, false, nullptr},
    {".clang-tidy", smith_new_clang_tidy, sizeof smith_new_clang_tidy, false, false, false, nullptr},
    {".claude/settings.json", smith_new_claude_settings, sizeof smith_new_claude_settings, false, false, false,
     nullptr},
    {"AGENTS.md", smith_new_agents, sizeof smith_new_agents, true, false, false, nullptr},
    {"knowledge/index.md", smith_new_knowledge_index, sizeof smith_new_knowledge_index, true, false, true, nullptr},
    {"knowledge/program.md", smith_new_knowledge_program, sizeof smith_new_knowledge_program, true, true, false,
     nullptr},
    {".agents/skills/@KEBAB@-docs/SKILL.md", smith_new_docs_skill, sizeof smith_new_docs_skill, true, false, false,
     nullptr},
    {".claude/skills", nullptr, 0, false, false, false, "../.agents/skills"},
};

constexpr size_t smith_new_name_max = 200;
constexpr size_t smith_new_written_max = 24;
constexpr size_t smith_new_path_max = 256;
constexpr size_t smith_new_text_max = 65536; // the largest file after its placeholders are filled
constexpr size_t smith_new_kebab_max = 40;
constexpr size_t smith_new_gitignore_lines_max = 100000;

typedef struct {
    int created;
    int kept;
    char written[smith_new_written_max][smith_new_path_max]; // the paths created, for git
    size_t written_count;
} SmithNewCounts;

// What the placeholders stand for: the program's name, and the same as a skill's name (lowercase, words joined by
// hyphens).
typedef struct {
    const char *name;
    char kebab[smith_new_kebab_max + 1];
} SmithNewNames;

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

// The name as a skill's: letters and digits in lowercase, any run of other characters one hyphen, none at the ends.
static void smith_new_kebab(const char *name, char *out) {
    size_t length = 0;
    for (const char *c = name; *c != '\0' && length < smith_new_kebab_max; c++) { // bounded by the name's length
        bool upper = *c >= 'A' && *c <= 'Z';
        bool word = upper || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9');
        if (word) {
            out[length++] = upper ? (char)(*c - 'A' + 'a') : *c;
        } else if (length > 0 && out[length - 1] != '-') {
            out[length++] = '-';
        }
    }
    while (length > 0 && out[length - 1] == '-') {
        length--;
    }
    if (length == 0) length = (size_t)snprintf(out, smith_new_kebab_max + 1, "project");
    out[length] = '\0';
}

// The bytes with @NAME@ and @KEBAB@ filled in, in out; returns the length. With no names the bytes are copied.
static size_t smith_new_expand(char *out, size_t capacity, const unsigned char *bytes, size_t size,
                               const SmithNewNames *names) {
    size_t used = 0;
    for (size_t i = 0; i < size; i++) { // bounded by the size of the content
        const char *with = nullptr;
        size_t hole = 0;
        if (names != nullptr && bytes[i] == '@') {
            if (size - i >= 6 && memcmp(bytes + i, "@NAME@", 6) == 0) {
                with = names->name;
                hole = 6;
            } else if (size - i >= 7 && memcmp(bytes + i, "@KEBAB@", 7) == 0) {
                with = names->kebab;
                hole = 7;
            }
        }
        size_t length = with != nullptr ? strlen(with) : 1;
        SMITH_ASSERT(used + length < capacity);
        if (with != nullptr) {
            memcpy(out + used, with, length);
            i += hole - 1;
        } else {
            out[used] = (char)bytes[i];
        }
        used += length;
    }
    out[used] = '\0';
    return used;
}

// Drops the last line, and the blank one before it.
static size_t smith_new_drop_last_line(const char *text, size_t length) {
    if (length > 0 && text[length - 1] == '\n') length--;
    while (length > 0 && text[length - 1] != '\n') {
        length--;
    }
    if (length > 1 && text[length - 2] == '\n') length--;
    return length;
}

// Writes a file, or makes a link, at a path nothing exists at, with the placeholders filled when the file is named.
static void smith_new_write(const SmithNewFile *file, const char *path, const SmithNewNames *names, bool skip_about) {
    if (file->link != nullptr) {
        if (symlink(file->link, path) != 0) smith_die(smith_exit_usage, "cannot write", path);
        return;
    }
    static char text[smith_new_text_max];
    size_t length = smith_new_expand(text, sizeof text, file->bytes, file->size, file->named ? names : nullptr);
    if (skip_about && file->lists_main) length = smith_new_drop_last_line(text, length);
    FILE *out = fopen(path, "wbx");
    if (out == nullptr) smith_die(smith_exit_usage, "cannot write", path);
    bool ok = fwrite(text, 1, length, out) == length;
    ok = fclose(out) == 0 && ok;
    if (!ok) smith_die(smith_exit_usage, "cannot write", path);
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
    SMITH_ASSERT(counts->written_count < smith_new_written_max && strlen(path) < smith_new_path_max);
    memcpy(counts->written[counts->written_count++], path, strlen(path) + 1);
}

// Each file is written only if nothing exists at its path. main.c is left out when the project has its own sources: a
// build.c already there, or another build system. What describes main.c is left out too, and when main.c was there
// before this run.
static void smith_new_files_write(const char *name, bool skip_main, bool skip_about, SmithNewCounts *counts) {
    SmithNewNames names = {.name = name};
    smith_new_kebab(name, names.kebab);
    for (size_t i = 0; i < sizeof smith_new_files / sizeof smith_new_files[0]; i++) {
        const SmithNewFile *file = &smith_new_files[i];
        if (skip_main && strcmp(file->path, "main.c") == 0) continue;
        char path[smith_new_path_max];
        smith_new_expand(path, sizeof path, (const unsigned char *)file->path, strlen(file->path), &names);
        bool exists = smith_new_exists(path);
        if (skip_about && file->about_main && !exists) continue;
        if (!exists) {
            smith_new_make_parents(path);
            smith_new_write(file, path, &names, skip_about);
        }
        smith_new_count(counts, path, exists);
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
    bool had_main = smith_new_exists("main.c");
    bool had_gitignore = smith_new_exists(".gitignore");
    bool had_settings = smith_new_exists(".claude/settings.json");
    SmithNewCounts counts = {};
    bool skip_main = has_build_c || other != nullptr;
    smith_new_files_write(name, skip_main, skip_main || had_main, &counts);
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
    printf("  smith run\n");
    return 0;
}

// `smith fmt`, `smith lint`, `smith lsp`, `smith dbg` and `smith dap` are clang-format, clang-tidy, clangd, lldb and
// lldb-dap, with the arguments unchanged, so a project's .clang-format and .clang-tidy apply as they do to the tools
// alone. They are recognised only as the exact first argument. A smith with clang inside runs the tool's own main in
// this process, from the same LLVM as the compiler; the stage0, and a caller who sets the tool's variable, run an
// external tool instead.
typedef struct {
    const char *command;  // smith's name for it
    const char *tool;     // the LLVM program
    const char *override; // the variable that names an external one to run instead
} SmithTool;

static const SmithTool smith_tools[] = {
    {"fmt", "clang-format", "SMITH_CLANG_FORMAT"},
    {"lint", "clang-tidy", "SMITH_CLANG_TIDY"},
    {"lsp", "clangd", "SMITH_CLANGD"},
    {"dbg", "lldb", "SMITH_LLDB"},
    {"dap", "lldb-dap", "SMITH_LLDB_DAP"},
};

static const SmithTool *smith_find_tool(const char *command) {
    for (size_t i = 0; i < sizeof smith_tools / sizeof smith_tools[0]; i++) {
        if (strcmp(command, smith_tools[i].command) == 0) return &smith_tools[i];
    }
    return nullptr;
}

#ifdef __APPLE__
// lldb starts the system's debugserver (LLDB_USE_SYSTEM_DEBUGSERVER): the one of the developer directory, else Xcode's,
// else the Command Line Tools'. The caller's LLDB_DEBUGSERVER_PATH wins. It is the one Apple signs with the debugger
// entitlements, which a debugserver made here could not have.
static void smith_debugserver_environment(void) {
    if (getenv("LLDB_DEBUGSERVER_PATH") != nullptr) return;
    static const char tail[] = "LLDB.framework/Resources/debugserver";
    const char *developer = getenv("DEVELOPER_DIR");
    char path[PATH_MAX];
    for (int i = 0; i < 3; i++) {
        int written = 0;
        if (i == 0 && developer != nullptr)
            written = snprintf(path, sizeof path, "%s/../SharedFrameworks/%s", developer, tail);
        if (i == 1) written = snprintf(path, sizeof path, "/Applications/Xcode.app/Contents/SharedFrameworks/%s", tail);
        if (i == 2)
            written =
                snprintf(path, sizeof path, "/Library/Developer/CommandLineTools/Library/PrivateFrameworks/%s", tail);
        SMITH_ASSERT(written >= 0 && (size_t)written < sizeof path);
        if (written > 0 && access(path, X_OK) == 0) {
            setenv("LLDB_DEBUGSERVER_PATH", path, 0);
            return;
        }
    }
}
#endif

#ifdef SMITH_IN_PROCESS
static int smith_tool_in_process(const SmithTool *tool, int argc, char **argv, const char *self) {
    char folder[PATH_MAX];
    char programs[PATH_MAX];
    char program[PATH_MAX];
    smith_resource_folder(folder); // clang's own folders: the tool's resource directory is the compiler's
    smith_programs_folder(folder, programs);
    int written = snprintf(program, sizeof program, "%s/bin/%s", programs, tool->tool);
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof program); // smith_programs_folder leaves room for it
    smith_resource_ensure(folder, programs, self);
    argv[0] = program; // the tool is told its own path in that folder, as the clang is
    if (strcmp(tool->command, "dbg") == 0 || strcmp(tool->command, "dap") == 0) {
        char argdumper[PATH_MAX];
        written = snprintf(argdumper, sizeof argdumper, "%s/bin/lldb-argdumper", programs);
        SMITH_ASSERT(written > 0 && (size_t)written < sizeof argdumper);
        setenv("LLDB_ARGDUMPER_PATH", argdumper, 0); // lldb expands a program's arguments through it
    }
    if (getenv("SMITH_VERBOSE") != nullptr) smith_show_command("in-process", program, argv);
    if (strcmp(tool->command, "fmt") == 0) return smith_format_main(argc, argv);
    if (strcmp(tool->command, "lint") == 0) return smith_tidy_main(argc, argv);
#ifdef SMITH_LLDB
    if (strcmp(tool->command, "dbg") == 0) return smith_lldb_main(argc, argv);
    if (strcmp(tool->command, "dap") == 0) return smith_lldb_dap_main(argc, argv);
#endif
    return smith_clangd_main(argc, argv);
}
#endif

// argv is main's, with argv[1] the command; the tool gets argv[1 ..] with the command replaced by its own path, so its
// arguments are unchanged.
static int smith_tool_main(const SmithTool *tool, int argc, char **argv, const char *self) {
    SMITH_ASSERT(tool != nullptr && argc >= 2 && argv[0] != nullptr);
#ifdef __APPLE__
    smith_sdk_environment(self); // clangd, and clang-tidy's compiles, find the SDK as the compiler does
    if (strcmp(tool->command, "dbg") == 0 || strcmp(tool->command, "dap") == 0) smith_debugserver_environment();
#endif
#ifdef SMITH_IN_PROCESS
    const char *override = getenv(tool->override);
    bool debugger = strcmp(tool->command, "dbg") == 0 || strcmp(tool->command, "dap") == 0;
    if ((override == nullptr || *override == '\0') && (!debugger || smith_has_lldb))
        return smith_tool_in_process(tool, argc - 1, argv + 1, self);
#endif
    char program[PATH_MAX];
    smith_find_clang(tool->tool, tool->override, self, program);
    argv[1] = program; // the full path, as for the compiler
    smith_run(program, argv + 1);
    return smith_exit_missing;
}

// smith's top level is only verbs. This table is where they are: a new verb is a row and, when it is none of these
// kinds, a case in smith_dispatch. A verb whose row says Project, ProjectBare or ToolOrProject runs the target of its
// name from the project's build.c (smith_project_main), and `smith build <name>` is refused for it.
typedef enum {
    SmithVerb_Cc,    // the compiler for C, Objective-C and Metal, with clang's arguments
    SmithVerb_Cxx,   // the same for C++
    SmithVerb_Build, // the project's build.c
    SmithVerb_New,
    SmithVerb_Fetch,
    SmithVerb_Cache,
    SmithVerb_Docs, // smith docs: checks AGENTS.md, knowledge/ and skills; the project's docs task with no arguments
    SmithVerb_Symbolize,     // smith symbolize: stack traces turned into lines
    SmithVerb_Version,       // smith version: the version line and the commit smith was built from
    SmithVerb_Tool,          // an LLVM tool, whose arguments are its own (smith_tools)
    SmithVerb_ToolOrProject, // the tool (or smith_prove) with arguments, the project's target of the name with none
    SmithVerb_Project,       // the project's target of the name; the arguments are the target's (smith_args)
    SmithVerb_ProjectBare,   // the same, with no arguments
    SmithVerb_Later,         // not available yet: says so and names the issue
} SmithVerbKind;

typedef struct {
    const char *name;
    SmithVerbKind kind;
    const char *later; // for SmithVerb_Later, what it waits for
} SmithVerb;

static const SmithVerb smith_verbs[] = {
    {"cc", SmithVerb_Cc, nullptr},
    {"c++", SmithVerb_Cxx, nullptr},
    {"build", SmithVerb_Build, nullptr},
    {"new", SmithVerb_New, nullptr},
    {"fetch", SmithVerb_Fetch, nullptr},
    {"cache", SmithVerb_Cache, nullptr},
    {"symbolize", SmithVerb_Symbolize, nullptr},
    {"docs", SmithVerb_Docs, nullptr},
    {"version", SmithVerb_Version, nullptr},
    {"fmt", SmithVerb_ToolOrProject, nullptr},
    {"lint", SmithVerb_ToolOrProject, nullptr},
    {"lsp", SmithVerb_Tool, nullptr},
    {"run", SmithVerb_Project, nullptr},
    {"test", SmithVerb_Project, nullptr},
    {"check", SmithVerb_ProjectBare, nullptr},
    {"prove", SmithVerb_ToolOrProject, nullptr},
    {"dbg", SmithVerb_Tool, nullptr},
    {"dap", SmithVerb_Tool, nullptr},
};

static const SmithVerb *smith_find_verb(const char *name) {
    for (size_t i = 0; i < sizeof smith_verbs / sizeof smith_verbs[0]; i++) {
        if (strcmp(name, smith_verbs[i].name) == 0) return &smith_verbs[i];
    }
    return nullptr;
}

// A name the project's build.c defines a target for, so `smith build <name>` is refused in favour of the verb.
static bool smith_is_project_verb(const char *name) {
    const SmithVerb *verb = smith_find_verb(name);
    return verb != nullptr &&
           (verb->kind == SmithVerb_Project || verb->kind == SmithVerb_ProjectBare ||
            verb->kind == SmithVerb_ToolOrProject || verb->kind == SmithVerb_Docs || verb->kind == SmithVerb_Later);
}

// The compiler: `smith cc <args>` and `smith c++ <args>`, and the links smith-cc and smith-c++, which act as them by
// their name. The program is argv[at]; --smith-which, when it comes first, names the compiler without running it.
static int smith_compile_main(int argc, char **argv, int at, bool plus, const char *self) {
    SMITH_ASSERT(at >= 0 && at < argc);
    int first = at + 1;
    bool which = first < argc && strcmp(argv[first], "--smith-which") == 0;
    if (which) first++;
    int count = argc - first;
    char **args = argv + first;
    switch (smith_classify(count, args)) {
    case SmithRoute_Mixed:
        smith_die(
            smith_exit_usage,
            ".metal and other inputs need different compilers and flags; compile the shaders in their own command",
            nullptr);
    case SmithRoute_Metal:
        return smith_route_metal(count, args, which, self);
    case SmithRoute_Clang:
        return smith_route_clang(argc, argv, first, which, plus, self);
    }
    return smith_exit_missing;
}

// The project verbs: run, test and check, and fmt and lint with no arguments. Each builds the target of its name, which
// the project's build.c defines, and runs it (docs/build-c.md). Arguments that start with -D are the build's options,
// as for `smith build`; the rest are the verb's own: the program's for run, the test runner's for test. They reach
// build.c as one option, -DSMITH_ARGS (each argument after a ";", see smith_encode_argument), which smith_args reads;
// `--` ends the -D options, so that a program can take one.
constexpr size_t smith_verb_args_max = 16384;

// Appends ";" and the argument to the text of -DSMITH_ARGS, with "%", ";" and newline as %25, %3B and %0A, so that no
// character of an argument is a separator or a newline (build.c's regeneration rule holds every -D option as a command
// word, and a command word holds none). smith_args in the runtime decodes it. Returns the new length.
static size_t smith_encode_argument(char *out, size_t capacity, size_t used, const char *arg) {
    SMITH_ASSERT(used < capacity);
    if (used + 2 > capacity) smith_die(smith_exit_usage, "the arguments are too long", arg);
    out[used++] = ';';
    for (const char *c = arg; *c != '\0'; c++) { // bounded by the argument
        const char *piece = *c == '%' ? "%25" : *c == ';' ? "%3B" : *c == '\n' ? "%0A" : nullptr;
        size_t length = piece != nullptr ? 3 : 1;
        if (used + length + 1 > capacity) smith_die(smith_exit_usage, "the arguments are too long", arg);
        if (piece != nullptr)
            memcpy(out + used, piece, length);
        else
            out[used] = *c;
        used += length;
    }
    out[used] = '\0';
    return used;
}

static int smith_project_main(const SmithVerb *verb, int count, char **args, const char *self) {
    SMITH_ASSERT(count >= 0 && verb != nullptr);
    if ((size_t)count > smith_build_args_max) smith_die(smith_exit_usage, "too many arguments", nullptr);
    static char *build_args[smith_build_args_max + 2];
    static char joined[smith_verb_args_max] = "-DSMITH_ARGS=";
    constexpr size_t prefix = sizeof "-DSMITH_ARGS=" - 1;
    size_t used = 0;
    size_t joined_used = prefix;
    bool options_end = false;
    for (int i = 0; i < count; i++) { // bounded by count, which is checked above
        const char *arg = args[i];
        if (!options_end && strcmp(arg, "--") == 0) {
            options_end = true;
        } else if (!options_end && strncmp(arg, "-D", 2) == 0) {
            build_args[used++] = args[i];
        } else {
            if (verb->kind == SmithVerb_ProjectBare) {
                smith_die(smith_exit_usage, "takes no arguments but -D options", verb->name);
            }
            joined_used = smith_encode_argument(joined, sizeof joined, joined_used, arg);
        }
    }
    joined[joined_used] = '\0';
    if (joined_used > prefix) build_args[used++] = joined;
    build_args[used++] = (char *)verb->name;
    return smith_build_main((int)used, build_args, self, verb->name);
}

// The hook for a verb that has not arrived: prove (issue #8) says so until their row changes kind.
static int smith_later_main(const SmithVerb *verb) {
    fprintf(stderr, "smith: %s is not available yet (%s)\n", verb->name, verb->later);
    return smith_exit_usage;
}

// Whether every argument is a -D option of the build, which neither clang-format nor clang-tidy has (none is true).
static bool smith_only_options(int count, char **args) {
    for (int i = 0; i < count; i++) { // bounded by count
        if (strncmp(args[i], "-D", 2) != 0) return false;
    }
    return true;
}

// `smith docs`: the check of AGENTS.md, knowledge/ and the skills, and the project's docs task with no arguments.
#include "runtime/docs.c"

// The commit smith was built from is in runtime/commit.c (`smith build` compiles it with -DSMITH_COMMIT and links it,
// and passes -DSMITH_COMMIT_LINKED here, so that a new commit recompiles that file alone). A smith built with one cc
// command does not link it and prints no commit.
#ifdef SMITH_COMMIT_LINKED
extern const char smith_commit[];
#else
static const char smith_commit[] = "";
#endif

// `smith version`: the version line, and after `commit` the commit when there is one.
static int smith_version_main() {
    bool known = smith_commit[0] != '\0';
    printf("smith 0.0 (milestone 0: a routing passthrough)%s%s\n", known ? " commit " : "", smith_commit);
    return 0;
}

static int smith_dispatch(const SmithVerb *verb, int argc, char **argv, const char *self) {
    switch (verb->kind) {
    case SmithVerb_Cc:
        return smith_compile_main(argc, argv, 1, false, self);
    case SmithVerb_Cxx:
        return smith_compile_main(argc, argv, 1, true, self);
    case SmithVerb_Build:
        return smith_build_main(argc - 2, argv + 2, self, nullptr);
    case SmithVerb_New:
        return smith_new_main(argc - 2, argv + 2, self);
    case SmithVerb_Fetch:
        return smith_fetch_main(argc - 2, argv + 2);
    case SmithVerb_Cache:
        return smith_cache_main(argc - 2, argv + 2);
    case SmithVerb_Tool:
        return smith_tool_main(smith_find_tool(verb->name), argc, argv, self);
    case SmithVerb_Docs:
        return smith_docs_main(verb, argc - 2, argv + 2, self);
    case SmithVerb_Symbolize:
        return smith_symbolize_main(argc - 2, argv + 2);
    case SmithVerb_Version:
        return smith_version_main();
    case SmithVerb_ToolOrProject: // with arguments it is the tool, with none (or only -D options) the project's
        if (!smith_only_options(argc - 2, argv + 2)) {
            if (strcmp(verb->name, "prove") == 0) return smith_prove(argc - 2, argv + 2, self);
            return smith_tool_main(smith_find_tool(verb->name), argc, argv, self);
        }
        return smith_project_main(verb, argc - 2, argv + 2, self);
    case SmithVerb_Project:
    case SmithVerb_ProjectBare:
        return smith_project_main(verb, argc - 2, argv + 2, self);
    case SmithVerb_Later:
        return smith_later_main(verb);
    }
    return smith_exit_missing;
}

static void smith_usage(FILE *out) {
    fputs("usage: smith <command> [<arguments>]\n"
          "  cc, c++                 compile and link as clang does (the links smith-cc and smith-c++ do the same)\n"
          "  build [<target>]        build the project from its build.c\n"
          "  run, test, check        run the project's program, its tests, or all its checks\n"
          "  fmt, lint               with no arguments, format or lint the project; with arguments, clang-format and\n"
          "                          clang-tidy\n"
          "  lsp                     clangd\n"
          "  dbg, dap                lldb, and lldb-dap (the Debug Adapter Protocol server)\n"
          "  docs                    check AGENTS.md, knowledge/ and the skills of the project\n"
          "  new, fetch, cache       start a project, fetch a package, show and clean the caches\n"
          "  version                 the version and the commit smith was built from\n",
          out);
}

// `smith foo.c -o foo` has no meaning: the top level is only verbs. The compiler is `smith cc`.
[[noreturn]] static void smith_bare_arguments(int argc, char **argv) {
    char line[4096];
    size_t used = 0;
    line[0] = '\0';
    for (int i = 1; i < argc; i++) { // bounded by argc
        int written = snprintf(line + used, sizeof line - used, " %s", argv[i]);
        if (written < 0 || (size_t)written >= sizeof line - used) break; // a hint, so a long command is cut short
        used += (size_t)written;
    }
    fprintf(stderr, "smith: %s is not a smith command; to compile, run `smith cc%s`\n", argv[1], line);
    exit(smith_exit_usage);
}

int main(int argc, char **argv) {
    SMITH_ASSERT(argc >= 1 && argv[0] != nullptr);
    char self[PATH_MAX];
    if (!smith_self_path(self)) smith_die(smith_exit_missing, "cannot determine the path of this executable", nullptr);

    const char *name = smith_base_name(argv[0]);
#ifdef SMITH_IN_PROCESS
    if (strcmp(name, "ld") == 0) return smith_route_linker(argc, argv);
    if (strcmp(name, "lldb-argdumper") == 0) return smith_argdumper(argc, argv);
#endif
    // The links act as the verbs by their name, for what needs one program path: CC=smith-cc, CMake,
    // compile_commands.json. So do the driver's own, bin/clang and bin/clang++ of the programs folder, which it runs
    // again with -cc1.
    if (strcmp(name, "llvm-symbolizer") == 0) return smith_llvm_symbolizer_main(argc, argv);
    if (strcmp(name, "smith-cc") == 0 || strcmp(name, "clang") == 0)
        return smith_compile_main(argc, argv, 0, false, self);
    if (strcmp(name, "smith-c++") == 0 || strcmp(name, "clang++") == 0)
        return smith_compile_main(argc, argv, 0, true, self);
    if (argc < 2) {
        smith_usage(stderr);
        return smith_exit_usage;
    }
    if (strcmp(argv[1], "--help") == 0) {
        smith_usage(stdout);
        return 0;
    }
    if (strcmp(argv[1], "--smith-sha256") == 0) return smith_sha256_main(argc - 2, argv + 2);
    if (strcmp(argv[1], "--smith-exec") == 0) return smith_exec_main(argc - 2, argv + 2);
    if (strcmp(argv[1], "--smith-header") == 0) return smith_lang_main(argc - 2, argv + 2, true);
    if (strcmp(argv[1], "--smith-lower") == 0) return smith_lang_main(argc - 2, argv + 2, false);
    const SmithVerb *verb = smith_find_verb(argv[1]);
    if (verb == nullptr) smith_bare_arguments(argc, argv);
    return smith_dispatch(verb, argc, argv, self);
}
