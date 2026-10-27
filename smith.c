// smith: a drop-in compiler driver.
//
// smith's top level is verbs (smith_verbs, below). `smith cc <args>` and `smith c++ <args>`, and the links `smith-cc`
// and `smith-c++` that act as them by their name, are a pure passthrough: they pick a backend by the kind of input,
// then run that compiler on the arguments, untouched:
//   - a .metal input goes to Apple's Metal compiler: `xcrun -sdk macosx metal`, or $SMITH_METAL if set, which smith
//     replaces itself with;
//   - anything else goes to upstream LLVM's clang. smith has clang linked in (shim/clang.cpp) and
//     runs clang's own main on the arguments, in its own process, unless $SMITH_CLANG names an external clang to run
//     instead (else the first clang on PATH that is not smith itself). `smith c++`, and the link `smith-c++`, use
//     clang++ ($SMITH_CLANGXX). On macOS it also picks the SDK through the environment, never a flag: see
//     smith_sdk_environment.
// It adds no flags and reads no configuration, so replacing a compiler with smith cannot change a build. See AGENTS.md.
//
// `smith build` compiles the project's build.c with an embedded runtime, runs it to write a ninja manifest, and then
// runs ninja. See knowledge/build-conventions.md. `smith fetch <url>` puts a package in the cache and `smith cache`
// shows and cleans the caches. `smith new` writes a ready project: main.smith (or main.c), a test, and what agents
// need.
#ifdef __linux__
// What glibc hides from C23 and macOS does not: nftw's FTW_PHYS, wait4 (knowledge/platform-layer.md).
// NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming): glibc's name for it
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <sys/sysctl.h> // runtime/slots.c reads a process through sysctl on macOS
#endif
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#include "runtime/base.h"
#include "scripts/pack.h"

#include <dirent.h>
#include <ftw.h>
#include <inttypes.h>
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
// shim/clangd.cpp: the files clangd formats and runs its checks by in a project that has none (smith's defaults).
void smith_clangd_defaults(const char *style, const char *tidy);
#ifdef SMITH_LLDB
constexpr bool smith_has_lldb = true;
// shim/lldb.cpp and shim/lldb_dap.cpp: the mains of lldb and lldb-dap (`smith dbg`, `smith dap`), run the same way. A
// smith built against an LLVM install without lldb (scripts/llvm.sh, the lldb step) runs an external lldb instead.
int smith_lldb_main(int argc, char **argv);
int smith_lldb_dap_main(int argc, char **argv);
#ifdef __linux__
int smith_lldb_server_main(int argc, char **argv); // shim/lldb_server.cpp: lldb's debug server on Linux
#endif
#else
constexpr bool smith_has_lldb = false;
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <stdint.h>
#endif

// Sizes. libc takes int, size_t and char *, so these are plain size_t and int, not the u32 and u64 of game's base.h.
constexpr size_t smith_metal_args_max = 4096; // arguments the metal route can forward; the clang route has no limit
constexpr size_t smith_metal_prefix_max = 4;  // xcrun -sdk macosx metal
constexpr size_t smith_metal_slack = 8;       // room in the argument vector beyond the limit

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
// The platform's identity of an executable, its clone, a file's time, a process's state and
// the resource pack's codec
// (knowledge/platform-layer.md).
#ifdef __APPLE__
#include "runtime/platform_macos.c"
#else
#include "runtime/platform_linux.c"
#endif

static unsigned char smith_resource_chunk[smith_pack_chunk_max];

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
        size_t got = smith_platform_decompress(smith_resource_chunk, sizeof smith_resource_chunk, data, file.stored);
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
// agent's) share one runtime folder, so they live in a folder named for the smith itself, its LC_UUID (on Linux its
// build-id): <data>/smith/programs/<uuid>/{bin/clang, bin/clang++, bin/ld, lib -> the runtime folder's lib}. Two smiths
// never share or remake each other's links, and a smith without lld has no `ld` to be found. The driver runs
// `bin/clang` again with -cc1 for a compile with more than one step, so it must be smith, which runs -cc1 in process; a
// copy keeps working when the smith it was made from is replaced or removed, as an install does. It is a clone (APFS
// copy on write: its own file, no bytes copied), never a hard link: macOS names a running program by the last name of
// its file that was looked up, and its policy daemon (syspolicyd) reads that name while it assesses the program,
// seconds later under load; every test's scratch data folder held a name of the same file and was removed, so the
// daemon found no file and killed the process (SIGKILL, "Killed: 9", issue #48). A hard link, then a symbolic link, is
// the fallback on a volume that cannot clone. The folder is built beside its place and renamed into it, so one that
// exists is whole.
//
// When a new one is made, the folders of binaries that are gone are removed (each records its binary, LC_UUID and
// path, in `binary`), and so is one not used for 31 days (using one touches it once a day) and a half built one older
// than an hour; never one a running smith holds (a shared lock on `.in-use`, taken by every smith for its life) and
// never one younger than an hour. Only the runtime folder must live forever.
constexpr time_t smith_programs_idle_seconds = 31L * 24 * 3600;
constexpr time_t smith_programs_touch_seconds = 24L * 3600;
constexpr time_t smith_programs_temporary_seconds = 3600;

// <data>/smith/programs/<uuid>, from the runtime folder <data>/smith/clang/<name>.
static void smith_programs_folder(const char *runtime, char *programs) {
    char uuid[33];
    smith_platform_self_id(uuid, sizeof uuid);
    const char *clang = strstr(runtime, "/smith/clang/");
    SMITH_ASSERT(clang != nullptr);
    int written = snprintf(programs, PATH_MAX, "%.*s/smith/programs/%s", (int)(clang - runtime), runtime, uuid);
    SMITH_ASSERT(written > 0 && written < PATH_MAX - 32);
}

// Whether the binary a programs folder was made for is gone: its `binary` file names the LC_UUID and the path, and
// the folder is orphaned when that path is missing or holds another build. A folder with no such file (an older
// smith made it) is not known to be orphaned.
static bool smith_programs_orphaned(const char *folder) {
    char record[PATH_MAX];
    snprintf(record, sizeof record, "%s/binary", folder);
    FILE *file = fopen(record, "r");
    if (file == nullptr) return false;
    char uuid[64] = "";
    char path[PATH_MAX] = "";
    bool read_both = fgets(uuid, sizeof uuid, file) != nullptr && fgets(path, sizeof path, file) != nullptr;
    fclose(file);
    if (!read_both) return false;
    uuid[strcspn(uuid, "\n")] = '\0';
    path[strcspn(path, "\n")] = '\0';
    struct stat info;
    if (stat(path, &info) != 0) return true;
    char found[33];
    return smith_platform_file_id(path, found) && strcmp(found, uuid) != 0;
}

// Takes the lock a running smith holds on its programs folder (a shared lock on <folder>/.in-use, kept for the
// life of the process and inherited by its children, which the kernel drops when it dies): `exclusive` is what the
// pruner takes, without waiting. Returns the descriptor, or -1 when another process holds the folder (or, for the
// pruner, when there is no such file and nothing to lock). The folder may have been removed between the open and
// the lock; then the file has no name left and the lock is refused.
static int smith_programs_lock(const char *folder, bool exclusive) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/.in-use", folder);
    int file = open(path, exclusive ? O_RDWR : O_RDWR | O_CREAT, 0644);
    if (file < 0) return -1;
    struct stat info;
    if (flock(file, exclusive ? LOCK_EX | LOCK_NB : LOCK_SH) != 0 || fstat(file, &info) != 0 || info.st_nlink == 0) {
        close(file);
        return -1;
    }
    return file;
}

// Removes what the folder `parent` holds that may go, but `keep`: a half built folder older than an hour; a folder
// whose binary is gone, or was rebuilt in place, or one idle for 31 days, when no process holds its lock (the
// pruner takes it and removes the folder while holding it) and it is older than an hour, which covers a smith that
// has started and not yet locked it.
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
        time_t idle = now - info.st_mtime;
        if (smith_ends_with(entry->d_name, ".tmp")) {
            if (idle > smith_programs_temporary_seconds) smith_remove_tree(path);
            continue;
        }
        if (idle <= smith_programs_temporary_seconds) continue;
        if (idle <= smith_programs_idle_seconds && !smith_programs_orphaned(path)) continue;
        int lock = smith_programs_lock(path, true);
        char probe[PATH_MAX];
        snprintf(probe, sizeof probe, "%s/.in-use", path);
        if (lock < 0 && access(probe, F_OK) == 0) continue; // a process holds it
        smith_remove_tree(path);
        if (lock >= 0) close(lock);
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
#if defined(__linux__) && defined(SMITH_LLDB)
    static const char *const names[] = {"clang", "clang++", "ld", "lldb-argdumper", "lldb-server"};
#else
    static const char *const names[] = {"clang", "clang++", "ld", "lldb-argdumper"};
#endif
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/bin", temporary);
    if (mkdir(path, 0755) != 0) smith_die(smith_exit_usage, "cannot create", path);
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        snprintf(path, sizeof path, "%s/bin/%s", temporary, names[i]);
        if (!smith_platform_clone(self, path)) smith_die(smith_exit_usage, "cannot link", path);
    }
    char target[PATH_MAX];
    snprintf(target, sizeof target, "%s/lib", runtime);
    snprintf(path, sizeof path, "%s/lib", temporary);
    if (symlink(target, path) != 0) smith_die(smith_exit_usage, "cannot link", path);
    char uuid[33];
    smith_platform_self_id(uuid, sizeof uuid);
    snprintf(path, sizeof path, "%s/binary", temporary);
    FILE *record = fopen(path, "w"); // the binary this folder is for: the pruner removes it when that is gone
    if (record == nullptr || fprintf(record, "%s\n%s\n", uuid, self) < 0 || fclose(record) != 0)
        smith_die(smith_exit_usage, "cannot write", path);
    if (rename(temporary, programs) != 0) { // another process of this smith may have won
        bool won = smith_is_folder(programs);
        smith_remove_tree(temporary);
        if (!won) smith_die(smith_exit_usage, "cannot create", programs);
    }
}

// Makes the runtime folder and this smith's programs exist; a second call, or a second process, finds them. Then
// this process takes the folder's shared lock for its life, so no prune removes it from under a running smith (a
// long-running `smith lsp` of a worktree that is gone). A prune may remove the folder between the two, which the
// refused lock tells: the folder is made again.
static void smith_resource_ensure(const char *runtime, const char *programs, const char *self) {
    static bool locked = false;
    if (!smith_is_folder(runtime)) smith_resource_unpack(runtime);
    for (int attempt = 0; attempt < 8 && !locked; attempt++) { // a prune racing us, each time a new folder
        struct stat info;
        if (stat(programs, &info) == 0 && S_ISDIR(info.st_mode)) {
            if (time(nullptr) - info.st_mtime > smith_programs_touch_seconds) utimes(programs, nullptr);
        } else {
            smith_programs_make(runtime, programs, self);
            char parent[PATH_MAX];
            snprintf(parent, sizeof parent, "%s", programs);
            *strrchr(parent, '/') = '\0';
            smith_programs_prune(parent, programs);
            smith_programs_sweep_legacy(runtime);
        }
        locked = smith_programs_lock(programs, false) >= 0; // the descriptor stays open, and is inherited
    }
    if (!locked) smith_die(smith_exit_usage, "cannot lock", programs);
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

#ifdef SMITH_HOOK
int smith_effects_check(int count, char **files, const char *stack); // shim/effects_infer.cpp

// The effect summary of an object: where the compile that makes the object writes it (the plugin) and where the link
// reads it. In a project (a `.smith` folder in the working folder) it is `.smith/effects/<the object's absolute path,
// `%` for `/`>.fx`, out of the build folders, otherwise `<object>.fx` beside the object. runtime/build_runtime.c has
// the same rule (`smith_effects_path`) for the outputs a build declares; tests/effects_infer_test.smith checks that
// they agree.
bool smith_effects_summary_path(const char *object, char *out, size_t capacity) {
    char absolute[PATH_MAX];
    while (strncmp(object, "./", 2) == 0)
        object += 2;
    int written = 0;
    if (object[0] == '/') {
        written = snprintf(absolute, sizeof absolute, "%s", object);
    } else {
        char folder[PATH_MAX];
        if (getcwd(folder, sizeof folder) == nullptr) return false;
        written = snprintf(absolute, sizeof absolute, "%s/%s", folder, object);
    }
    if (written <= 0 || (size_t)written >= sizeof absolute) return false;
    struct stat info = {};
    if (stat(".smith", &info) != 0 || !S_ISDIR(info.st_mode)) {
        written = snprintf(out, capacity, "%s.fx", object);
        return written > 0 && (size_t)written < capacity;
    }
    for (char *c = absolute; *c != '\0'; c++) {
        if (*c == '/') *c = '%';
    }
    written = snprintf(out, capacity, ".smith/effects/%s.fx", absolute);
    return written > 0 && (size_t)written < capacity;
}

// Whether a path is in the folder of temporary files (the objects clang makes for `smith cc a.c b.c -o p` are there,
// with their summaries beside them, and are removed after the link).
static bool smith_in_temp_folder(const char *path) {
    const char *temp = getenv("TMPDIR");
    if (temp != nullptr && *temp != '\0' && strncmp(path, temp, strlen(temp)) == 0) return true;
    return strncmp(path, "/var/folders/", 13) == 0 || strncmp(path, "/private/var/folders/", 21) == 0 ||
           strncmp(path, "/tmp/", 5) == 0 || strncmp(path, "/private/tmp/", 13) == 0;
}

// A link of objects (a command with `.o` inputs and no -c, -E, -S or -fsyntax-only): when some of them have an effect
// summary (written by the compile, `smith_effects_summary_path` says where), the program's check runs before the link
// and its errors stop
// it (skill smith-lang-effects). It runs in the compiler's command when the objects are named, and in the linker the
// driver starts (`smith cc a.c b.c -o p` makes its objects in the temporary folder); the first leaves
// SMITH_EFFECTS_CHECKED for the second. Returns the status to exit with, or -1 for a command that is no such link.
static int smith_check_effects(int count, char **args) {
    // Sized from the command: every object with a summary is read, however many there are (a cap here once skipped the
    // objects past it, and a function of a skipped one looked like a function with no source).
    size_t candidates = 0;
    for (int i = 0; i < count; i++) {
        if (args[i][0] != '-' && smith_ends_with(args[i], ".o")) candidates += 1;
    }
    if (candidates == 0) return -1;
    size_t room = candidates * (PATH_MAX + 2 * sizeof(char *));
    void *block = mmap(nullptr, room, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (block == MAP_FAILED) smith_die(smith_exit_missing, "cannot allocate the effect summaries of the link", args[0]);
    char **file_list = (char **)block;
    const char **objects = (const char **)(file_list + candidates);
    char (*paths)[PATH_MAX] = (char (*)[PATH_MAX])(objects + candidates);
    size_t found = 0;
    const char *program = "a.out";
    for (int i = 0; i < count; i++) {
        if (strcmp(args[i], "-c") == 0 || strcmp(args[i], "-E") == 0 || strcmp(args[i], "-S") == 0 ||
            strcmp(args[i], "-fsyntax-only") == 0 || strcmp(args[i], "-###") == 0) {
            return -1;
        }
        if (strcmp(args[i], "-o") == 0 && i + 1 < count) {
            i++; // the output is no input
            program = args[i];
            continue;
        }
        if (args[i][0] == '-' || !smith_ends_with(args[i], ".o")) continue;
        if (!smith_effects_summary_path(args[i], paths[found], PATH_MAX) || access(paths[found], R_OK) != 0) continue;
        file_list[found] = paths[found];
        objects[found] = args[i];
        found += 1;
    }
    if (found == 0) {
        munmap(block, room);
        return -1;
    }
    char stack[PATH_MAX]; // the worst-case stack of the program's functions, beside it: `<program>.stack`
    int written = snprintf(stack, sizeof stack, "%s.stack", program);
    int status =
        smith_effects_check((int)found, file_list, written > 0 && (size_t)written < sizeof stack ? stack : nullptr);
    for (size_t i = 0; i < found; i++) { // the summary of a temporary object goes with it
        if (smith_in_temp_folder(objects[i])) unlink(file_list[i]);
    }
    munmap(block, room);
    return status;
}
#endif

static int smith_route_linker(int argc, char **argv) {
    const char *override = getenv("SMITH_LINKER");
    if (smith_has_linker_override()) {
        argv[0] = (char *) override;
        execv(override, argv);
        smith_die(smith_exit_missing, "cannot run the linker named by SMITH_LINKER", override);
    }
#ifdef SMITH_HOOK
    if (getenv("SMITH_EFFECTS_CHECKED") == nullptr) {
        int status = smith_check_effects(argc - 1, argv + 1);
        if (status > 0) return status;
    }
#endif
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

#include "runtime/lang.h"

constexpr size_t smith_smith_args_max = 8192; // arguments of a command with .smith inputs

// The .smith files of the command, for the compile of each: an `import foo` that finds no foo.h looks for foo.smith
// among them first (the hook reads SMITH_LANG_INPUTS, one path to a line). The environment is smith's own and inherited
// by the compiles the driver runs.
static void smith_set_smith_inputs(int argc, char **argv, int first) {
    static char inputs[smith_smith_args_max];
    size_t used = 0;
    inputs[0] = 0;
    for (int i = first; i < argc; i++) { // bounded by argc
        if (argv[i][0] == '-' && smith_takes_value(argv[i]) && i + 1 < argc) {
            i += 1;
        } else if (argv[i][0] != '-' && smith_ends_with(argv[i], ".smith")) {
            size_t length = strlen(argv[i]);
            if (used + length + 2 > sizeof inputs) break; // a command with that many keeps the rest to the path search
            memcpy(inputs + used, argv[i], length);
            inputs[used + length] = '\n';
            used += length + 1;
            inputs[used] = 0;
        }
    }
    setenv("SMITH_LANG_INPUTS", inputs, 1);
}

// The flags of the command that decide where a module's imports are found and what they declare (-I, -isystem, -D,
// -std,
// ...), one to a line, for the compile that makes another module's body file (issue #107): it is given the same ones.
static void smith_set_smith_flags(int argc, char **argv, int first) {
    static char flags[smith_smith_args_max];
    static const char *const separate[] = {"-I", "-isystem", "-iquote", "-include", "-isysroot", "-D", "-U"};
    static const char *const joined[] = {"-I", "-isystem", "-iquote", "-D", "-U", "-std=", "-isysroot", "-F"};
    size_t used = 0;
    flags[0] = 0;
    for (int i = first; i < argc; i++) { // bounded by argc
        bool pair = false;
        bool keep = false;
        for (size_t k = 0; k < sizeof separate / sizeof separate[0]; k++)
            pair = pair || strcmp(argv[i], separate[k]) == 0;
        for (size_t k = 0; k < sizeof joined / sizeof joined[0]; k++)
            keep = keep || strncmp(argv[i], joined[k], strlen(joined[k])) == 0;
        size_t need = strlen(argv[i]) + 1 + (pair && i + 1 < argc ? strlen(argv[i + 1]) + 1 : 0);
        if ((!pair && !keep) || used + need + 1 > sizeof flags) {
            i += argv[i][0] == '-' && smith_takes_value(argv[i]) && i + 1 < argc ? 1 : 0;
            continue;
        }
        used += (size_t)snprintf(flags + used, sizeof flags - used, "%s\n", argv[i]);
        if (pair && i + 1 < argc) used += (size_t)snprintf(flags + used, sizeof flags - used, "%s\n", argv[++i]);
    }
    setenv("SMITH_LANG_FLAGS", flags, 1);
    char self[PATH_MAX];
    if (smith_self_path(self)) setenv("SMITH_LANG_SELF", self, 1);
}

// Rewrites the command for the .smith inputs it names (the rule is smith_lang_command, runtime/lang_command.c, which
// `smith lsp` applies to every command clangd compiles with), and tells the hook the .smith files of the command.
// Returns the new argument count, or 0 when the command stays as it is.
static int smith_rewrite_smith_inputs(int argc, char **argv, int first, char **out) {
    int count = smith_lang_command(argc, argv, first, out, smith_smith_args_max);
    if (count < 0) smith_die(smith_exit_usage, "too many arguments with .smith inputs", nullptr);
    if (count > 0) {
        smith_set_smith_inputs(argc, argv, first);
        smith_set_smith_flags(argc, argv, first);
    }
    return count;
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
    if (smith_runs_in_process(plus ? "SMITH_CLANGXX" : "SMITH_CLANG")) {
        return smith_route_in_process(argc, argv, first, which, plus, self);
    }
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

// smith prove: the prover (Alive2, Bitwuzla and Z3) is linked into smith when the LLVM install it was built
// against has it (build.c); a smith built without it says so.
static int smith_prove(int argc, char **argv, const char *self) {
#ifdef SMITH_PROVE
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
    smith_die(smith_exit_usage, "this smith was built without the prover", "smith build llvm, then smith build");
#endif
}

// smith build.

// The build program's header and runtime travel inside smith, so `smith build` needs nothing installed beside it and
// `cc -std=gnu23 smith.c -o smith` still builds all of smith.
static constexpr unsigned char smith_build_source[] = {
#embed "lib/smith/build.smith"
};
static constexpr unsigned char smith_build_runtime[] = {
#embed "runtime/build_runtime.c"
};
// The runtime includes this file, so it is written beside it.
static constexpr unsigned char smith_build_package[] = {
#embed "runtime/package.c"
};
static constexpr unsigned char smith_build_gzip[] = {
#embed "runtime/gzip.c"
};
static constexpr unsigned char smith_build_xz[] = {
#embed "runtime/xz.c"
};
static constexpr unsigned char smith_build_untar[] = {
#embed "runtime/untar.c"
};

// smith's libraries: each header and source is written into the project's .smith/ on every `smith build`, where build.c
// finds the source with smith_library and the compiles find the header through -I .smith/include.
//
// The registry. A library is its header and its source. A C library (lib/<name>.c, written to .smith/lib/<name>.c) has
// a hand-written header, include/smith/<name>.h, embedded. A .smith module (lib/smith/<name>.smith, written to
// .smith/lib/smith/<name>.smith, where the module smith::<name> lives at its path) has no header in git: smith embeds
// the source, and this binary writes the header from it (`smith cc --smith-header`, smith_write_lang_headers), so the
// program that reads a header is the one that wrote it. Porting a library is one change in each of two places below:
// its source's #embed path, and the `false` of its row in smith_libraries.
static constexpr unsigned char smith_arena_source[] = {
#embed "lib/smith/arena.smith"
};

static constexpr unsigned char smith_contract_source[] = {
#embed "lib/smith/contract.smith"
};

static constexpr unsigned char smith_inflate_source[] = {
#embed "lib/smith/inflate.smith"
};

static constexpr unsigned char smith_io_source[] = {
#embed "lib/smith/io.smith"
};

static constexpr unsigned char smith_json_header[] = {
#embed "include/smith/json.h"
};
static constexpr unsigned char smith_json_source[] = {
#embed "lib/json.c"
};

static constexpr unsigned char smith_process_source[] = {
#embed "lib/smith/process.smith"
};
static constexpr unsigned char smith_random_source[] = {
#embed "lib/smith/random.smith"
};

static constexpr unsigned char smith_text_source[] = {
#embed "lib/smith/text.smith"
};
// The HTTPS client is smith's own for now, not a library a build.c can ask for: it goes to .smith/include/smith/ and
// .smith/internal/, where the runtime's package.c finds it, and not to .smith/lib/ (which smith_library lists).
static constexpr unsigned char smith_autodiff_source[] = {
#embed "lib/smith/autodiff.smith"
};
static constexpr unsigned char smith_effects_source[] = {
#embed "lib/smith/effects.smith"
};
static constexpr unsigned char smith_failing_source[] = {
#embed "lib/smith/failing.smith"
};
static constexpr unsigned char smith_files_source[] = {
#embed "lib/smith/files.smith"
};
static constexpr unsigned char smith_format_source[] = {
#embed "lib/smith/format.smith"
};
static constexpr unsigned char smith_clock_source[] = {
#embed "lib/smith/clock.smith"
};
static constexpr unsigned char smith_console_source[] = {
#embed "lib/smith/console.smith"
};
static constexpr unsigned char smith_entropy_source[] = {
#embed "lib/smith/entropy.smith"
};
static constexpr unsigned char smith_env_source[] = {
#embed "lib/smith/env.smith"
};
static constexpr unsigned char smith_math_source[] = {
#embed "lib/smith/math.smith"
};
static constexpr unsigned char smith_mem_source[] = {
#embed "lib/smith/mem.smith"
};
static constexpr unsigned char smith_net_source[] = {
#embed "lib/smith/net.smith"
};

static constexpr unsigned char smith_ascii_source[] = {
#embed "lib/smith/ascii.smith"
};
static constexpr unsigned char smith_categorical_source[] = {
#embed "lib/smith/categorical.smith"
};
static constexpr unsigned char smith_slice_source[] = {
#embed "lib/smith/slice.smith"
};

static constexpr unsigned char smith_handlers_header[] = {
#embed "include/smith/handlers.h"
};
static constexpr unsigned char smith_handlers_source[] = {
#embed "lib/handlers.c"
};

static constexpr unsigned char smith_embedded_http_header[] = {
#embed "include/smith/http.h"
};
static constexpr unsigned char smith_embedded_http_source[] = {
#embed "lib/http.c"
};
static constexpr unsigned char smith_test_source[] = {
#embed "lib/smith/test.smith"
};

static constexpr unsigned char smith_trace_header[] = {
#embed "include/smith/trace.h"
};
static constexpr unsigned char smith_trace_source[] = {
#embed "lib/trace.c"
};

typedef struct {
    const char *name;
    const unsigned char *header;
    size_t header_size;
    const unsigned char *source;
    size_t source_size;
    bool lang;         // the source is a .smith module, not C
    const char *flags; // the flags the library declares, which `smith build` adds when it compiles it, or nullptr
} SmithLibrary;

// One row per library, in order of name: SMITH_LIBRARY(name, lang) uses the arrays smith_<name>_header (a C library's
// hand-written header; a .smith module has none, its header is written from its source) and smith_<name>_source.
// SMITH_LIBRARY_FLAGS adds the flags the library declares (one string, a flag a line), which the build adds after the
// project's own whenever it compiles the library: random needs -ffp-contract=off, whatever the project passes.
#define SMITH_HEADER_true(library) nullptr
#define SMITH_HEADER_SIZE_true(library) 0
#define SMITH_HEADER_false(library) smith_##library##_header
#define SMITH_HEADER_SIZE_false(library) sizeof smith_##library##_header
#define SMITH_HEADER(library, is_lang) SMITH_HEADER_##is_lang(library)
#define SMITH_HEADER_SIZE(library, is_lang) SMITH_HEADER_SIZE_##is_lang(library)
#define SMITH_LIBRARY_FLAGS(library, is_lang, declared) \
    {#library, \
     SMITH_HEADER(library, is_lang), \
     SMITH_HEADER_SIZE(library, is_lang), \
     smith_##library##_source, \
     sizeof smith_##library##_source, \
     is_lang, \
     declared}
#define SMITH_LIBRARY(library, is_lang) SMITH_LIBRARY_FLAGS(library, is_lang, nullptr)

// clang-format off
static const SmithLibrary smith_libraries[] = {
    SMITH_LIBRARY(arena, true),
    SMITH_LIBRARY(ascii, true),
    SMITH_LIBRARY(autodiff, true),
    SMITH_LIBRARY(categorical, true),
    SMITH_LIBRARY(clock, true),
    SMITH_LIBRARY(console, true),
    SMITH_LIBRARY(contract, true),
    SMITH_LIBRARY(effects, true),
    SMITH_LIBRARY(entropy, true),
    SMITH_LIBRARY(failing, true),
    SMITH_LIBRARY(env, true),
    SMITH_LIBRARY(files, true),
    SMITH_LIBRARY(format, true),
    SMITH_LIBRARY(handlers, false),
    SMITH_LIBRARY(inflate, true),
    SMITH_LIBRARY(io, true),
    SMITH_LIBRARY(json, false),
    SMITH_LIBRARY(math, true),
    SMITH_LIBRARY(mem, true),
    SMITH_LIBRARY(process, true),
    SMITH_LIBRARY(net, true),
    SMITH_LIBRARY_FLAGS(random, true, "-ffp-contract=off\n"),
    SMITH_LIBRARY(slice, true),
    SMITH_LIBRARY(test, true),
    SMITH_LIBRARY(text, true),
    SMITH_LIBRARY(trace, false),
};
// clang-format on

// `smith symbolize` and smith as llvm-symbolizer: stack traces turned into lines.
#include "runtime/symbolize.c"
// `smith --smith-explain`: the values of a failed check, read from the stopped test with lldb.
#include "runtime/explain.c"
// Packages, shared with the build runtime: the cache, the download and SHA-256.
#include "runtime/package.c"
// `smith cache`, and the lock a build holds on the caches.
#include "runtime/cache.c"
// What a run costs: the wall and CPU time of each task, and the smith processes it started.
#include "runtime/account.c"
// Quiet output: the logs, the excerpt of a failure and the line of a stage.
#include "runtime/quiet.c"
// The host-wide slots that limit heavy jobs, so that local work cannot starve CI.
#include "runtime/slots.c"
// The executor that runs the build graph, in place of ninja.
#include "runtime/executor.c"
// The lowering of .smith files, and the header pass that needs no compiler.
#include "runtime/lang.c"
#include "runtime/lang_lex.c"
// The rule for the command of a .smith file, shared with `smith lsp`.
#include "runtime/lang_command.c"
// A Makefile read as GNU make reads it and written as a build.c: `smith build --dump-config` (issue #44).
// (memory and text, the expander, the reader, the targets, then build.c; blank lines keep clang-format from sorting)
#include "runtime/make.c"

#include "runtime/make_expand.c"

#include "runtime/make_read.c"

#include "runtime/make_graph.c"

#include "runtime/make_emit.c"

#include "runtime/make_cc.c"

#include "runtime/make_sub.c"

#include "runtime/make_class.c"

#include "runtime/make_print.c"

#include "runtime/make_translate.c"

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
    int written = snprintf(temporary, sizeof temporary, "%s.%d.tmp", path,
                           (int)getpid());                           // unique, so parallel first runs do not meet
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

// `smith cc --smith-tree-sitter`: the path of the tree-sitter parser for .smith files, a shared library `smith build`
// compiled from tree-sitter/grammar.js and linked into smith (runtime/tree_sitter_data.c), which the Neovim plugin
// loads (knowledge/neovim.md). It is written once into the runtime folder, named for its bytes, so smiths of one LLVM
// with different parsers never meet and a smith never replaces a library another has loaded. The Linux seed has none.
#ifdef SMITH_TREE_SITTER
extern const unsigned char smith_tree_sitter_library[]; // runtime/tree_sitter_data.c
extern const size_t smith_tree_sitter_library_size;
#endif

static int smith_tree_sitter_main(const char *self) {
#ifdef SMITH_TREE_SITTER
    char folder[PATH_MAX];
    char programs[PATH_MAX];
    smith_resource_folder(folder);
    smith_programs_folder(folder, programs);
    smith_resource_ensure(folder, programs, self);
    uint64_t key = 14695981039346656037ULL;                     // FNV-1a over the library's bytes
    for (size_t i = 0; i < smith_tree_sitter_library_size; i++) // bounded by the library's size
        key = (key ^ smith_tree_sitter_library[i]) * 1099511628211ULL;
#ifdef __APPLE__
    const char *extension = "dylib";
#else
    const char *extension = "so";
#endif
    char path[PATH_MAX];
    int written = snprintf(path, sizeof path, "%s/tree-sitter/smith-%016" PRIx64 ".%s", folder, key, extension);
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof path);
    smith_make_folders(path, false);
    smith_write_if_changed(path, smith_tree_sitter_library, smith_tree_sitter_library_size);
    printf("%s\n", path);
    return 0;
#else
    (void)self;
    smith_die(smith_exit_usage,
              "this smith was built without the tree-sitter parser (a seed build); rebuild with `smith build`",
              nullptr);
#endif
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

// A project with no build file is built by convention (knowledge/build-conventions.md): the build program is the two
// calls of the conventions with their defaults, compiled like any build.c, from this file.
static constexpr char smith_conventions_path[] = ".smith/conventions.c";
static constexpr char smith_conventions_source[] =
    "// The build of a project that has no build file: smith's conventions, with their defaults "
    "(knowledge/build-conventions.md).\n"
    "#include <smith/build.h>\n"
    "\n"
    "void build(SmithBuild *b) {\n"
    "    smith_app(b, &(SmithApp){});\n"
    "    smith_app_tests(b, &(SmithAppTests){});\n"
    "}\n";

// Which file is the source of the build program: the project's build.c, its build.smith, or smith's conventions (a
// folder with neither). Both files at once, and a folder with no build file and no entry point, are errors.
typedef enum { SmithBuildFile_C, SmithBuildFile_Smith, SmithBuildFile_Conventions } SmithBuildFile;

// The Makefile make would read, by its exact name (macOS finds `makefile` for `Makefile`), or nullptr.
static bool smith_has_makefile(void) {
    return smith_make_find_makefile() != nullptr;
}

// A build file that has something in it: `smith build --dump-config > build.c` makes the file empty before it runs.
static bool smith_has_build_file(const char *name) {
    struct stat info;
    return stat(name, &info) == 0 && info.st_size > 0;
}

// The folder has a Makefile and no build file: it is the project's build, so the conventions are not used behind it.
[[noreturn]] static void smith_makefile_here(void) {
    fprintf(stderr,
            "smith: this folder has a %s and no build.c or build.smith, so smith does not build it by convention\n"
            "Translate it with `smith build --dump-config > build.c` (knowledge/makefile-translation.md), or "
            "delete it to build by convention.\n",
            smith_make_find_makefile());
    exit(smith_exit_usage);
}

// What smith looked for, and how to write a build file. The folder has no build file and no entry point.
[[noreturn]] static void smith_no_convention(void) {
    fputs("smith: no build.c in this folder, and no main.smith or main.c to build by convention\n"
          "smith build runs the build.c or build.smith of the project's root; without one it builds main.smith or "
          "main.c\n"
          "and what they use (knowledge/build-conventions.md). Add a main.smith or main.c (`smith new` writes one), or "
          "write a build.c.\n",
          stderr);
    exit(smith_exit_usage);
}

static SmithBuildFile smith_build_file(void) {
    bool has_c = smith_has_build_file("build.c");
    bool has_smith = smith_has_build_file("build.smith");
    if (has_c && has_smith) {
        smith_die(smith_exit_usage, "both build.c and build.smith are here; a project has one build file, keep one",
                  nullptr);
    }
    if (has_c) return SmithBuildFile_C;
    if (has_smith) {
#ifndef SMITH_HOOK
        smith_die(
            smith_exit_usage,
            "build.smith needs a smith whose clang has the token hook (patches/clang-token-hook.patch, applied by "
            "`smith build llvm`); this one has none",
            nullptr);
#endif
        return SmithBuildFile_Smith;
    }
    if (smith_has_makefile()) smith_makefile_here();
    if (access("main.smith", F_OK) == 0 || access("main.c", F_OK) == 0) return SmithBuildFile_Conventions;
    smith_no_convention();
}

// The libraries the build program links, .smith modules that the smith doing the build compiles whatever compiler
// compiles build.c itself (an external SMITH_CLANG knows no .smith): the runtime's package.c and the executor use them
// through their headers. random declares -ffp-contract=off.
static const char *const smith_build_program_libraries[] = {"arena", "contract", "inflate", "io", "random"};

// The manifest that compiles build.c into .smith/program with the clang `smith` would pick. When that is smith itself
// (in_process) the program is `<smith> cc`, since smith's top level is verbs.
static size_t smith_build_program_manifest(char *out, size_t capacity, const char *self, const char *clang,
                                           bool in_process, const char *build_source) {
    char word[PATH_MAX * 2];
    char quoted[PATH_MAX * 2 + 4];
    char own_word[PATH_MAX * 2];
    smith_quote_word(word, sizeof word, clang);
    smith_quote_word(own_word, sizeof own_word, self);
    int made = snprintf(quoted, sizeof quoted, "%s%s", word, in_process ? " cc" : "");
    SMITH_ASSERT(made > 0 && (size_t)made < sizeof quoted);
    // A .smith file (a library, or a build.smith) is smith's own language, so it compiles as C2y (smith cc names no
    // standard) by smith itself, never by an external clang.
    size_t source_length = strlen(build_source);
    bool is_smith = source_length > 6 && strcmp(build_source + source_length - 6, ".smith") == 0;
    int written =
        snprintf(out, capacity,
                 "# Written by smith build. It compiles the build file into .smith/program.\n"
                 "ninja_required_version = 1.10\n"
                 "builddir = .smith/build-program\n"
                 "rule cc\n"
                 "  command = %s -std=gnu23 -g -O0 -Wall -Wextra -I .smith/include -MMD -MF $out.d -c $in -o $out\n"
                 "  depfile = $out.d\n"
                 "  deps = gcc\n"
                 "  description = smith: compile $in\n"
                 "rule cc_smith\n"
                 "  command = env -u SMITH_CLANG %s cc -g -O0 -Wall -Wextra $flags -I .smith/include -MMD -MF $out.d "
                 "-c $in -o $out\n"
                 "  depfile = $out.d\n"
                 "  deps = gcc\n"
                 "  description = smith: compile $in\n"
                 "rule link\n"
                 "  command = %s $in -o $out\n"
                 "  description = smith: link $out\n"
                 "build .smith/build-program/build.o: %s %s\n"
                 "build .smith/build-program/runtime.o: cc .smith/runtime.c\n",
                 quoted, own_word, quoted, is_smith ? "cc_smith" : "cc", build_source);
    SMITH_ASSERT(written > 0 && (size_t)written < capacity);
    char objects[1024] = ".smith/build-program/build.o .smith/build-program/runtime.o";
    size_t objects_length = strlen(objects);
    for (size_t i = 0; i < sizeof smith_build_program_libraries / sizeof smith_build_program_libraries[0]; i++) {
        const char *name = smith_build_program_libraries[i];
        const char *flags = strcmp(name, "random") == 0 ? "-ffp-contract=off" : "";
        int line = snprintf(out + written, capacity - (size_t)written,
                            "build .smith/build-program/%s.o: cc_smith .smith/lib/smith/%s.smith\n  flags = %s\n", name,
                            name, flags);
        SMITH_ASSERT(line > 0 && (size_t)line < capacity - (size_t)written);
        written += line;
        int more =
            snprintf(objects + objects_length, sizeof objects - objects_length, " .smith/build-program/%s.o", name);
        SMITH_ASSERT(more > 0 && (size_t)more < sizeof objects - objects_length);
        objects_length += (size_t)more;
    }
    int linked = snprintf(out + written, capacity - (size_t)written, "build .smith/program: link %s\n", objects);
    SMITH_ASSERT(linked > 0 && (size_t)linked < capacity - (size_t)written);
    return (size_t)written + (size_t)linked;
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

// The question of the header pass (runtime/lang_stream.c, smith_lang_resolve_imports): a module that imports several C
// headers has in its header only those one of whose names an export uses, and only the compiler knows which header
// declared a name. So smith asks itself: `smith cc -fsyntax-only` on a file of nothing but the module's imports (the
// hook, shim/hook.cpp, with SMITH_LANG_ASK_IMPORTS) writes, for each name, the import that declared it. A smith with no
// clang inside cannot compile a .smith file; the answer is then no and the header keeps every import.
// The probe: the module's imports and the conditionals around them, as a .smith file.
static bool smith_lang_write_probe(const SmithLang *lang, const char *path) {
    FILE *file = fopen(path, "w");
    if (file == nullptr) return false;
    char prelude[2048]; // the facts the conditionals ask
    size_t prelude_used = smith_lang_target_include_prelude(lang, prelude, sizeof prelude, 0);
    fprintf(file, "%.*s", (int)prelude_used, prelude);
    for (size_t at = 0; at < lang->include_used;) { // each line
        size_t end = at;
        while (end < lang->include_used && lang->include_body[end] != '\n')
            end += 1;
        const char *line = lang->include_body + at;
        if (end - at > 9 && memcmp(line, "#include ", 9) == 0) {
            fprintf(file, "import %.*s;\n", (int)(end - at - 9), line + 9);
        } else {
            fprintf(file, "%.*s\n", (int)(end - at), line);
        }
        at = end + 1;
    }
    return fclose(file) == 0;
}

// Runs `smith cc -fsyntax-only` on the probe, which writes its answers to `answers`; true if it succeeded.
static bool smith_lang_run_probe(const SmithLang *lang, const char *probe, const char *answers) {
    static char names[smith_lang_cuses_max * (smith_lang_name_max + 8)];
    size_t used = 0;
    for (size_t i = 0; i < lang->c_uses_count; i++) { // bounded by the table
        int written = snprintf(names + used, sizeof names - used, "%s%s%s", i > 0 ? "," : "",
                               lang->c_uses[i].tag ? "struct " : "", smith_lang_str(lang->c_uses[i].name));
        if (written <= 0 || used + (size_t)written >= sizeof names) return false;
        used += (size_t)written;
    }
    char self[PATH_MAX];
    char source[PATH_MAX];
    if (!smith_self_path(self)) return false;
    snprintf(source, sizeof source, "%s", smith_header_source != nullptr ? smith_header_source : ".");
    char *slash = strrchr(source, '/');
    if (slash != nullptr) {
        *slash = 0;
    } else {
        snprintf(source, sizeof source, ".");
    }
    char *args[smith_header_flags_max + 8];
    size_t count = 0;
    args[count++] = self;
    args[count++] = (char *)"cc";
    args[count++] = (char *)"-fsyntax-only";
    args[count++] = (char *)"-w";
    args[count++] = (char *)"-iquote";
    args[count++] = source;
    for (size_t i = 0; i < smith_header_flag_count; i++) // bounded by the flags
        args[count++] = (char *)smith_header_flags[i];
    args[count++] = (char *)probe;
    args[count] = nullptr;
    setenv("SMITH_LANG_ASK_IMPORTS", names, 1);
    setenv("SMITH_LANG_IMPORTS_OUT", answers, 1);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    int status = -1;
    if (posix_spawn(&pid, self, &actions, nullptr, args, environ) == 0) {
        int waited = -1;
        for (int i = 0; i < 1000 && waited < 0; i++) // only a signal interrupts the wait
            waited = waitpid(pid, &status, 0);
    }
    posix_spawn_file_actions_destroy(&actions);
    unsetenv("SMITH_LANG_ASK_IMPORTS");
    unsetenv("SMITH_LANG_IMPORTS_OUT");
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// The answers: a line `name<TAB>spelling` for each name asked, in order (the spelling empty when no import declared
// it).
static bool smith_lang_read_answers(const SmithLang *lang, const char *answers, const char **spellings) {
    static char spelled[smith_lang_cuses_max][smith_lang_name_max + 8];
    FILE *result = fopen(answers, "r");
    if (result == nullptr) return false;
    char line[2 * smith_lang_name_max];
    size_t index = 0;
    while (index < lang->c_uses_count && fgets(line, sizeof line, result) != nullptr) { // one line per name
        char *tab = strchr(line, '\t');
        line[strcspn(line, "\n")] = 0;
        if (tab != nullptr && tab[1] != 0) {
            snprintf(spelled[index], sizeof spelled[index], "%s", tab + 1);
            spellings[index] = spelled[index];
        }
        index += 1;
    }
    fclose(result);
    return index == lang->c_uses_count;
}

static bool smith_lang_ask_compiler(const SmithLang *lang, const char **spellings) {
    const char *temporary = getenv("TMPDIR");
    char folder[PATH_MAX];
    snprintf(folder, sizeof folder, "%s/smith-imports-XXXXXX",
             temporary != nullptr && *temporary != 0 ? temporary : "/tmp");
    if (mkdtemp(folder) == nullptr) return false;
    char probe[PATH_MAX + 16];
    char answers[PATH_MAX + 16];
    snprintf(probe, sizeof probe, "%s/probe.smith", folder);
    snprintf(answers, sizeof answers, "%s/answers", folder);
    bool good = smith_lang_write_probe(lang, probe) && smith_lang_run_probe(lang, probe, answers) &&
                smith_lang_read_answers(lang, answers, spellings);
    unlink(probe);
    unlink(answers);
    rmdir(folder);
    return good;
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

// The headers of the .smith modules smith embeds (the libraries and smith::build) are written from the embedded
// sources by this binary, in parallel, into .smith/include/smith/, so the program that reads a header is the one that
// wrote it and no header is in git. The stamp names the binary that wrote them (its size and modification time): the
// next run writes nothing while the stamp and every header are there, and a new binary writes them again. A header is
// replaced only when its bytes change, so what includes it is not rebuilt for nothing.
constexpr char smith_headers_stamp[] = ".smith/include/smith/.written-by";
constexpr size_t smith_headers_max = 32;
constexpr size_t smith_header_bytes_max = (size_t)1 << 18;

typedef struct {
    char source[PATH_MAX];
    char header[PATH_MAX];
    char scratch[PATH_MAX + 32];
    pid_t pid;
} SmithHeaderJob;

static bool smith_binary_identity(const char *self, char *out, size_t capacity) {
    struct stat info = {};
    if (stat(self, &info) != 0) return false;
    int written = snprintf(out, capacity, "%lld %lld\n", (long long)info.st_size, (long long)info.st_mtime);
    return written > 0 && (size_t)written < capacity;
}

// Starts `self cc --smith-header source -o scratch`, quietly except for its errors.
static bool smith_header_job_start(SmithHeaderJob *job, const char *self) {
    snprintf(job->scratch, sizeof job->scratch, "%s.%d.tmp", job->header, (int)getpid());
    char *const args[] = {(char *)self, (char *)"cc", (char *)"--smith-header", job->source, (char *)"-o",
                          job->scratch, nullptr};
    return posix_spawn(&job->pid, self, nullptr, nullptr, args, environ) == 0;
}

static bool smith_header_job_finish(SmithHeaderJob *job) {
    static unsigned char bytes[smith_header_bytes_max];
    int status = 0;
    int waited = -1;
    for (int i = 0; i < 1000 && waited < 0; i++) // bounded; only a signal interrupts the wait
        waited = waitpid(job->pid, &status, 0);
    bool good = waited >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    FILE *file = good ? fopen(job->scratch, "rb") : nullptr;
    if (file == nullptr) return false;
    size_t size = fread(bytes, 1, sizeof bytes, file);
    fclose(file);
    SMITH_ASSERT(size < sizeof bytes);
    smith_write_if_changed(job->header, bytes, size);
    unlink(job->scratch);
    return true;
}

static void smith_header_job_add(SmithHeaderJob *jobs, size_t *count, const char *name, const char *source_folder) {
    SMITH_ASSERT(*count < smith_headers_max);
    SmithHeaderJob *job = &jobs[(*count)++];
    snprintf(job->source, sizeof job->source, "%s/smith/%s.smith", source_folder, name);
    snprintf(job->header, sizeof job->header, ".smith/include/smith/%s.h", name);
}

// SMITH_HEADERS_FROM names a folder of headers (another project's .smith/include/smith) that a project copies instead
// of generating its own, when the folder's stamp names this binary: the headers are a function of the binary alone. The
// tests set it to the repository's (tests/scratch.smith), so a scratch project starts no `--smith-header` process
// (about 20, 2 s of CPU, for each of the 1300 projects of a run). A folder that is missing a header, or stamped by
// another binary, is ignored and the headers are generated as usual.
static bool smith_headers_copy(const SmithHeaderJob *jobs, size_t count, const char *identity) {
    static unsigned char bytes[smith_header_bytes_max];
    const char *from = getenv("SMITH_HEADERS_FROM");
    if (from == nullptr || from[0] != '/') return false;
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/.written-by", from);
    if (!smith_file_has_bytes(path, (const unsigned char *)identity, strlen(identity))) return false;
    for (int pass = 0; pass < 2; pass++) {   // the first pass only checks that all are there
        for (size_t i = 0; i < count; i++) { // bounded by the jobs
            const char *slash = strrchr(jobs[i].header, '/');
            snprintf(path, sizeof path, "%s/%s", from, slash + 1);
            FILE *file = fopen(path, "rb");
            if (file == nullptr) return false;
            size_t size = fread(bytes, 1, sizeof bytes, file);
            bool whole = feof(file) != 0 && size < sizeof bytes;
            fclose(file);
            if (!whole) return false;
            if (pass == 1) smith_write_if_changed(jobs[i].header, bytes, size);
        }
    }
    return true;
}

// Writes the headers that are missing, or all of them when the stamp is not this binary's. The sources are written
// already (the libraries' by smith_build_write_libraries, smith::build's by the caller).
static void smith_write_lang_headers(const char *self) {
    static SmithHeaderJob jobs[smith_headers_max];
    size_t count = 0;
    for (size_t i = 0; i < sizeof smith_libraries / sizeof smith_libraries[0]; i++) {
        if (smith_libraries[i].lang) smith_header_job_add(jobs, &count, smith_libraries[i].name, ".smith/lib");
    }
    smith_header_job_add(jobs, &count, "build", ".smith/internal");
    char identity[64];
    bool named = smith_binary_identity(self, identity, sizeof identity);
    bool current =
        named && smith_file_has_bytes(smith_headers_stamp, (const unsigned char *)identity, strlen(identity));
    bool copied = named && !current && smith_headers_copy(jobs, count, identity);
    bool good = true;
    for (size_t i = 0; i < count; i++) {
        jobs[i].pid = 0;
        if (copied || (current && access(jobs[i].header, R_OK) == 0)) continue;
        good = smith_header_job_start(&jobs[i], self) && good;
    }
    for (size_t i = 0; i < count; i++) {
        if (jobs[i].pid != 0) good = smith_header_job_finish(&jobs[i]) && good;
    }
    if (!good)
        smith_die(smith_exit_usage, "cannot write the headers of smith's libraries (smith cc --smith-header)", nullptr);
    if (named && !current)
        smith_write_if_changed(smith_headers_stamp, (const unsigned char *)identity, strlen(identity));
}

// Writes each library's header to .smith/include/smith/<name>.h and its source to .smith/lib/<name>.c, or for a .smith
// module to .smith/lib/smith/<name>.smith (removing a <name>.c an older smith left, which smith_library would find
// first).
static void smith_build_write_libraries(void) {
    for (size_t i = 0; i < sizeof smith_libraries / sizeof smith_libraries[0]; i++) {
        const SmithLibrary *library = &smith_libraries[i];
        char path[PATH_MAX];
        int written = snprintf(path, sizeof path, ".smith/include/smith/%s.h", library->name);
        SMITH_ASSERT(written > 0 && (size_t)written < sizeof path);
        if (!library->lang) smith_write_if_changed(path, library->header, library->header_size);
        if (library->lang) {
            written = snprintf(path, sizeof path, ".smith/lib/%s.c", library->name);
            SMITH_ASSERT(written > 0 && (size_t)written < sizeof path);
            unlink(path);
            written = snprintf(path, sizeof path, ".smith/lib/smith/%s.smith", library->name);
        } else {
            written = snprintf(path, sizeof path, ".smith/lib/%s.c", library->name);
        }
        SMITH_ASSERT(written > 0 && (size_t)written < sizeof path);
        smith_write_if_changed(path, library->source, library->source_size);
        // The flags the library declares go beside its source (the build adds them to its compile).
        char *extension = strrchr(path, '.');
        SMITH_ASSERT(extension != nullptr);
        written = snprintf(extension, sizeof path - (size_t)(extension - path), ".flags");
        SMITH_ASSERT(written > 0 && (size_t)written < sizeof path - (size_t)(extension - path));
        if (library->flags != nullptr)
            smith_write_if_changed(path, (const unsigned char *)library->flags, strlen(library->flags));
        else
            unlink(path);
    }
}

// Writes the libraries' sources and headers, and smith::build's header, into the project's .smith/ (the editor's
// `smith lsp` does the same, so a header is there on first use).
static void smith_write_embedded_headers(const char *self) {
    smith_make_folder(".smith");
    smith_make_folder(".smith/include");
    smith_make_folder(".smith/include/smith");
    smith_make_folder(".smith/lib");
    smith_make_folder(".smith/lib/smith");
    smith_make_folder(".smith/internal");
    smith_make_folder(".smith/internal/smith");
    smith_write_if_changed(".smith/.gitignore", (const unsigned char *)"*\n", 2);
    smith_build_write_libraries();
    smith_write_if_changed(".smith/internal/smith/build.smith", smith_build_source, sizeof smith_build_source);
    smith_write_lang_headers(self);
}

static void smith_build_prepare(const char *self, const char *clang, bool in_process, SmithBuildFile file) {
    smith_make_folder(".smith");
    smith_make_folder(".smith/include");
    smith_make_folder(".smith/include/smith");
    smith_make_folder(".smith/lib");
    smith_make_folder(".smith/lib/smith");
    smith_make_folder(".smith/internal");
    smith_make_folder(".smith/build-program");
    static char manifest[smith_build_manifest_max];
    size_t length = smith_build_program_manifest(manifest, sizeof manifest, self, clang, in_process,
                                                 file == SmithBuildFile_Conventions ? smith_conventions_path
                                                 : file == SmithBuildFile_Smith     ? "build.smith"
                                                                                    : "build.c");
    // Git ignores smith's folder with no change to the project, as pytest, ruff and mypy do for their caches.
    smith_write_if_changed(".smith/.gitignore", (const unsigned char *)"*\n", 2);
    smith_write_embedded_headers(self);
    smith_write_if_changed(".smith/include/smith/http.h", smith_embedded_http_header,
                           sizeof smith_embedded_http_header);
    smith_write_if_changed(".smith/internal/http.c", smith_embedded_http_source, sizeof smith_embedded_http_source);
    if (file == SmithBuildFile_Conventions)
        smith_write_if_changed(smith_conventions_path, (const unsigned char *)smith_conventions_source,
                               sizeof smith_conventions_source - 1);
    smith_write_if_changed(".smith/runtime.c", smith_build_runtime, sizeof smith_build_runtime);
    smith_write_if_changed(".smith/package.c", smith_build_package, sizeof smith_build_package);
    smith_write_if_changed(".smith/gzip.c", smith_build_gzip, sizeof smith_build_gzip);
    smith_write_if_changed(".smith/xz.c", smith_build_xz, sizeof smith_build_xz);
    smith_write_if_changed(".smith/untar.c", smith_build_untar, sizeof smith_build_untar);
    smith_write_if_changed(".smith/build-program/build.ninja", (const unsigned char *)manifest, length);
}

// What compiles and links build.c: an external clang, found as for any compile, or smith itself when clang is in it
// (then true, and the commands are `smith cc ...`).
static bool smith_build_compiler(const char *self, char *out) {
    if (smith_runs_in_process("SMITH_CLANG")) {
        int written = snprintf(out, PATH_MAX, "%s", self);
        SMITH_ASSERT(written > 0 && written < PATH_MAX); // self came from a PATH_MAX buffer
        return true;
    }
    smith_find_clang("clang", "SMITH_CLANG", self, out);
    return false;
}

// The flags of ninja that `smith build` takes, for the executor: -j, -k, -n, -v, --quiet and -t. -l, -d and -w are read
// and ignored. Anything else that starts with a dash is an error; the rest are the targets.
constexpr int smith_build_jobs_max = (int)smith_exec_jobs_max;

static int smith_build_default_jobs(void) {
    long cpus = smith_slots_cores();
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
    char *program_manifest_argv[] = {ninja, (char *)"--quiet", (char *)"-f", (char *)".smith/build-program/build.ninja",
                                     nullptr};
    int status = smith_build_spawn(ninja, program_manifest_argv);
    if (status == 0) status = smith_build_spawn(program_argv[0], program_argv);
    if (status != 0) return status;
    smith_run(ninja, ninja_argv);
    return smith_exit_missing;
}

// The verbs whose meaning is fixed in every project: build.c may not name a task after one (runtime/build_runtime.c),
// and `smith build <name>` says to run the verb.
static bool smith_is_project_verb(const char *name) {
    static const char *const names[] = {"fmt", "lint", "docs", "prove", "check", "run", "test"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (strcmp(name, names[i]) == 0) return true;
    }
    return false;
}

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

// `smith build --dump-config`: the build file that does what the conventions do, written out
// (knowledge/build-conventions.md). Its language is the entry point's: a main.smith gets a build.smith, a main.c a
// build.c, each in the highest-level API with the defaults spelled out as fields. It goes to stdout, so `smith build
// --dump-config > build.smith` makes it the project's; everything else goes to stderr.

// The folder's name, which is the program's by default (smith_app's .name), as the text of a C string.
static void smith_dump_name(void) {
    char folder[PATH_MAX];
    if (getcwd(folder, sizeof folder) == nullptr)
        smith_die(smith_exit_missing, "cannot read the current folder", nullptr);
    const char *slash = strrchr(folder, '/');
    const char *name = slash != nullptr ? slash + 1 : folder;
    putchar('"');
    for (const char *c = name; *c != '\0'; c++) {
        if (*c == '"' || *c == '\\') putchar('\\');
        putchar(*c);
    }
    putchar('"');
}

static void smith_dump_call(const char *open, const char *entry, const char *close) {
    fputs(open, stdout);
    printf("        .entry = \"%s\",\n        .name = ", entry);
    smith_dump_name();
    fputs(",\n", stdout);
    fputs(close, stdout);
}

static void smith_dump_smith(void) {
    fputs(
        "// build.smith: what `smith build` does with no build file (knowledge/build-conventions.md), written out by\n"
        "// `smith build --dump-config`. Change a field, or replace a call with the low-level calls it expands to.\n"
        "module build;\n"
        "\n"
        "import smith::build;\n"
        "\n"
        "export void build(build::Build *b) {\n",
        stdout);
    smith_dump_call("    build::app(b, &(build::App){\n", "main.smith", "    });\n");
    fputs("    build::app_tests(b, &(build::AppTests){.folder = \"tests\"});\n}\n", stdout);
}

static void smith_dump_c(void) {
    fputs("// build.c: what `smith build` does with no build file (knowledge/build-conventions.md), written out by\n"
          "// `smith build --dump-config`. Change a field, or replace a call with the low-level calls it expands to.\n"
          "#include <smith/build.h>\n"
          "\n"
          "void build(SmithBuild *b) {\n",
          stdout);
    smith_dump_call("    smith_app(b, &(SmithApp){\n", "main.c", "    });\n");
    fputs("    smith_app_tests(b, &(SmithAppTests){.folder = \"tests\"});\n}\n", stdout);
}

// Copies a file to standard output from its start.
static void smith_copy_to_stdout(FILE *from) {
    char chunk[65536];
    SMITH_ASSERT(fseek(from, 0, SEEK_SET) == 0);
    for (size_t n = fread(chunk, 1, sizeof chunk, from); n > 0; n = fread(chunk, 1, sizeof chunk, from))
        fwrite(chunk, 1, n, stdout);
}

// The translation of a Makefile, formatted by `smith fmt` (smith's .clang-format, or the project's) so that the
// build.c it makes passes `smith fmt --dry-run`. Where the formatter cannot run, the text as it is written.
static void smith_make_dump(const char *makefile) {
    FILE *raw = tmpfile();
    FILE *formatted = tmpfile();
    if (raw == nullptr || formatted == nullptr) {
        smith_make_translate(makefile, stdout);
        return;
    }
    smith_make_translate(makefile, raw);
    bool formatted_ok = smith_make_format_files(raw, formatted);
    smith_copy_to_stdout(formatted_ok ? formatted : raw);
    fclose(raw);
    fclose(formatted);
}

// The one place that decides what a folder dumps. A project that has a build file is already explicit.
static int smith_build_dump_config(void) {
    bool has_c = smith_has_build_file("build.c");
    bool has_smith = smith_has_build_file("build.smith");
    if (has_c || has_smith) {
        fprintf(stderr, "smith: %s is already this project's build file, so it is already explicit; nothing to dump\n",
                has_c ? "build.c" : "build.smith");
        return 0;
    }
    if (smith_has_makefile()) { // the project's real build, whether or not a main is here too (issue #44)
        smith_make_dump(smith_make_find_makefile());
        return 0;
    }
    bool main_smith = access("main.smith", F_OK) == 0;
    bool main_c = access("main.c", F_OK) == 0;
    if (main_smith && main_c) {
        smith_die(smith_exit_usage,
                  "both main.smith and main.c are here; --dump-config needs one entry point, remove one", nullptr);
    }
    if (main_smith) {
        smith_dump_smith();
    } else if (main_c) {
        smith_dump_c();
    } else {
        smith_no_convention();
    }
    return 0;
}

// Whether this smith can build a smith::autodiff program: Enzyme is linked into its linker (shim/lld.cpp), and the
// compile and the link run in its own process (SMITH_CLANG and SMITH_LINKER run external ones, which have no Enzyme).
static bool smith_can_differentiate(void) {
#ifdef SMITH_ENZYME
    return smith_runs_in_process("SMITH_CLANG") && !smith_has_linker_override();
#else
    return false;
#endif
}

// A build's compilers are the links `smith-cc` and `smith-c++` that a build file names; they are found on PATH, so the
// folder of the running smith goes first when it holds smith-cc: a smith under test compiles with itself, never with
// the installed one. A caller's CC (a path) or SMITH_CLANG still wins, since they bypass the name.
static void smith_prefer_own_links(const char *self) {
    char folder[PATH_MAX];
    int written = snprintf(folder, sizeof folder, "%s", self);
    SMITH_ASSERT(written > 0 && written < PATH_MAX);
    char *slash = strrchr(folder, '/');
    if (slash == nullptr) return;
    *slash = '\0';
    char link[PATH_MAX];
    written = snprintf(link, sizeof link, "%s/smith-cc", folder);
    if (written <= 0 || written >= PATH_MAX || !smith_is_executable(link)) return;
    const char *old = getenv("PATH");
    char path[PATH_MAX * 4];
    written = snprintf(path, sizeof path, "%s:%s", folder, old == nullptr ? "" : old);
    if (written > 0 && (size_t)written < sizeof path) setenv("PATH", path, 1);
}

// The program's `--effects=` argument: whether the compiles write effect summaries, and the smith that writes them,
// from the build's own configuration (SMITH_EFFECTS and SMITH_SELF as set above), so the manifest does not depend on
// the environment the program is run in (the regeneration rule repeats it).
static void smith_build_effects_argument(const char *self, char *out, size_t capacity) {
    const char *on = getenv("SMITH_EFFECTS");
    bool yes = on != nullptr && strcmp(on, "yes") == 0;
    int written = snprintf(out, capacity, "--effects=%s", yes ? self : "no");
    SMITH_ASSERT(written > 0 && (size_t)written < capacity);
}

// verb is nullptr for `smith build`, and the project verb that runs this build otherwise (runtime/verbs.c passes
// nullptr: its targets are smith's own, smith_needs and smith_tests).
static int smith_build_main(int count, char **args, const char *self, const char *verb) {
    SMITH_ASSERT(count >= 0);
    if (verb == nullptr && count == 1 && strcmp(args[0], "--dump-config") == 0) return smith_build_dump_config();
    if ((size_t)count <= smith_build_args_max && verb == nullptr) smith_build_refuse_verbs(count, args);
    SmithBuildFile file = smith_build_file();
    smith_cache_hold();
    smith_prefer_own_links(self);
    setenv("SMITH_ENZYME", smith_can_differentiate() ? "yes" : "no",
           1);                     // whether smith::autodiff programs can be built
    setenv("SMITH_SELF", self, 1); // the build program finds smith here: `smith symbolize` for the tests' failures
    {
        char id[33];
        smith_platform_self_id(id, sizeof id);
        setenv("SMITH_BUILD_ID", id, 1); // the module cache is this smith's (smith_modules, knowledge/caches.md)
    }
#ifdef SMITH_HOOK
    // Whether a compile writes an effect summary (the plugin runs in smith's own clang, not in an external one): the
    // build declares it an output of the compile only then.
    const char *external = getenv("SMITH_CLANG");
    setenv("SMITH_EFFECTS", external == nullptr || *external == '\0' ? "yes" : "no", 1);
#else
    setenv("SMITH_EFFECTS", "no", 1);
#endif
    const char *forced = getenv("SMITH_NINJA");
    bool use_ninja = forced != nullptr && *forced != '\0';
    char ninja[PATH_MAX] = "";
    if (use_ninja) smith_build_find_ninja(self, ninja);
    if ((size_t)count > smith_build_args_max) smith_die(smith_exit_usage, "too many arguments", nullptr);
    static char *ninja_argv[smith_build_args_max + smith_build_ninja_prefix + 1];
    static char *program_argv[smith_build_args_max + 4];
    ninja_argv[0] = ninja;
    ninja_argv[1] = (char *)"-f";
    ninja_argv[2] = (char *)".smith/build.ninja";
    program_argv[0] = (char *)".smith/program";
    program_argv[1] = (char *)".smith/build.ninja";
    size_t ninja_used = smith_build_ninja_prefix;
    char effects_argument[PATH_MAX + 16];
    smith_build_effects_argument(self, effects_argument, sizeof effects_argument);
    program_argv[2] = effects_argument;
    size_t program_used = 3;
    smith_build_split(count, args, ninja_argv, program_argv, &ninja_used, &program_used);
    SmithExecOptions options =
        smith_build_options((int)(ninja_used - smith_build_ninja_prefix), ninja_argv + smith_build_ninja_prefix);
    options.verb = verb;

    char clang[PATH_MAX];
    bool in_process = smith_build_compiler(self, clang);
#ifdef __APPLE__
    smith_sdk_environment(self); // the compiles inherit it, as do the programs build.c runs
#endif
    smith_build_prepare(self, clang, in_process, file);
    if (use_ninja) return smith_build_with_ninja(ninja, ninja_argv, program_argv);
    SmithIo io = smith_io_make(0, nullptr);
    SmithExecOptions program_options = {.jobs = options.jobs, .failures_allowed = 1, .quiet = true};
    int status = smith_exec_run(&io, ".smith/build-program/build.ninja", &program_options);
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
    SmithPackageFault error;
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
static constexpr unsigned char smith_new_main_smith[] = {
#embed "templates/new/main.smith"
};
static constexpr unsigned char smith_new_primes_smith[] = {
#embed "templates/new/primes.smith"
};
static constexpr unsigned char smith_new_primes_test_smith[] = {
#embed "templates/new/primes_test.smith"
};
static constexpr unsigned char smith_new_main_c[] = {
#embed "templates/new-c/main.c"
};
static constexpr unsigned char smith_new_primes_h[] = {
#embed "templates/new-c/primes.h"
};
static constexpr unsigned char smith_new_primes_c[] = {
#embed "templates/new-c/primes.c"
};
static constexpr unsigned char smith_new_primes_test_c[] = {
#embed "templates/new-c/primes_test.c"
};
static constexpr unsigned char smith_new_gitignore[] = {
#embed "templates/new/gitignore"
};
static constexpr unsigned char smith_new_claude_settings[] = {
#embed "templates/new/claude-settings.json"
};
static constexpr unsigned char smith_new_agents[] = {
#embed "templates/new/agents.md"
};
static constexpr unsigned char smith_new_knowledge_index[] = {
#embed "templates/new/knowledge-index.md"
};
static constexpr unsigned char smith_new_knowledge_program[] = {
#embed "templates/new/knowledge-program.md"
};
static constexpr unsigned char smith_new_knowledge_program_c[] = {
#embed "templates/new/knowledge-program-c.md"
};
static constexpr unsigned char smith_new_docs_skill[] = {
#embed "templates/new/docs-skill.md"
};

// smith's own style and checks, which `smith fmt` and `smith lint` use where a project has no file
// (smith_defaults_apply).
static constexpr unsigned char smith_defaults_clang_format[] = {
#embed "templates/defaults/clang-format"
};
static constexpr unsigned char smith_defaults_clang_tidy[] = {
#embed "templates/defaults/clang-tidy"
};

typedef enum { SmithNewLanguage_Smith, SmithNewLanguage_C } SmithNewLanguage;

typedef struct {
    const char *path;
    const unsigned char *bytes;
    size_t size;
    bool named;      // @NAME@ is replaced by the program's name, and @KEBAB@ by the same in kebab-case
    bool about_main; // describes main, so a project that keeps its own sources does not get it
    bool lists_main; // its last line lists what describes main, which that project does not get either
    bool any;        // written for both languages; else only for `language`
    SmithNewLanguage language;
    const char *link; // not a file: a symbolic link to this
} SmithNewFile;

#define SMITH_NEW_FILE(path, bytes, named, about_main, lists_main) \
    {path, bytes, sizeof(bytes), named, about_main, lists_main, true, SmithNewLanguage_Smith, nullptr}
#define SMITH_NEW_ONLY(path, bytes, language, about_main) \
    {path, bytes, sizeof(bytes), true, about_main, false, false, language, nullptr}

// The program's files, then what every project gets. No build file and no .clang-format or .clang-tidy: smith builds by
// convention and has defaults for both (issue #43).
static const SmithNewFile smith_new_files[] = {
    SMITH_NEW_ONLY("main.smith", smith_new_main_smith, SmithNewLanguage_Smith, false),
    SMITH_NEW_ONLY("primes.smith", smith_new_primes_smith, SmithNewLanguage_Smith, false),
    SMITH_NEW_ONLY("primes_test.smith", smith_new_primes_test_smith, SmithNewLanguage_Smith, false),
    SMITH_NEW_ONLY("main.c", smith_new_main_c, SmithNewLanguage_C, false),
    SMITH_NEW_ONLY("primes.h", smith_new_primes_h, SmithNewLanguage_C, false),
    SMITH_NEW_ONLY("primes.c", smith_new_primes_c, SmithNewLanguage_C, false),
    SMITH_NEW_ONLY("primes_test.c", smith_new_primes_test_c, SmithNewLanguage_C, false),
    SMITH_NEW_FILE(".gitignore", smith_new_gitignore, false, false, false),
    SMITH_NEW_FILE(".claude/settings.json", smith_new_claude_settings, false, false, false),
    SMITH_NEW_FILE("AGENTS.md", smith_new_agents, true, false, false),
    SMITH_NEW_FILE("knowledge/index.md", smith_new_knowledge_index, true, false, true),
    SMITH_NEW_ONLY("knowledge/program.md", smith_new_knowledge_program, SmithNewLanguage_Smith, true),
    SMITH_NEW_ONLY("knowledge/program.md", smith_new_knowledge_program_c, SmithNewLanguage_C, true),
    SMITH_NEW_FILE(".agents/skills/@KEBAB@-docs/SKILL.md", smith_new_docs_skill, true, false, false),
    {".claude/skills", nullptr, 0, false, false, false, true, SmithNewLanguage_Smith, "../.agents/skills"},
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

// The sources of the template's program: the main and what it calls and tests (a file of one language that no concept
// describes).
static bool smith_new_is_source(const SmithNewFile *file) {
    return !file->any && !file->about_main;
}

// Each file is written only if nothing exists at its path. Only the files of the language asked for are written. The
// sources (main and the files that go with it) are left out when the project has its own: a build file already there,
// or another build system. What describes main is left out too, and when a main was there before this run.
static void smith_new_files_write(const char *name, SmithNewLanguage language, bool skip_main, bool skip_about,
                                  SmithNewCounts *counts) {
    SmithNewNames names = {.name = name};
    smith_new_kebab(name, names.kebab);
    for (size_t i = 0; i < sizeof smith_new_files / sizeof smith_new_files[0]; i++) {
        const SmithNewFile *file = &smith_new_files[i];
        if (!file->any && file->language != language) continue;
        if (skip_main && smith_new_is_source(file)) continue;
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
    static const char *const wanted[] = {"/build/", "/.cache/"};
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
static const char *smith_new_enter(const char *argument, char *folder_name) {
    if (argument != nullptr) {
        if (!smith_new_is_name(argument)) smith_new_bad_name(argument, false);
        smith_make_folder(argument);
        if (chdir(argument) != 0) smith_die(smith_exit_usage, "cannot enter", argument);
        return argument;
    }
    char here[PATH_MAX];
    if (getcwd(here, sizeof here) == nullptr) smith_die(smith_exit_usage, "cannot find the current folder", nullptr);
    int written = snprintf(folder_name, PATH_MAX, "%s", smith_base_name(here));
    SMITH_ASSERT(written >= 0 && written < PATH_MAX);
    if (!smith_new_is_name(folder_name)) smith_new_bad_name(folder_name, true);
    return folder_name;
}

constexpr char smith_new_usage[] = "usage: smith new [--language smith|c] [<name>]";

// The arguments of `smith new`: --language smith|c (also --language=<value>), and at most one name. Returns the name,
// or nullptr for the current folder. `--help` prints the usage and exits.
static const char *smith_new_parse(int count, char **args, SmithNewLanguage *language) {
    const char *name = nullptr;
    *language = SmithNewLanguage_Smith;
    for (int i = 0; i < count; i++) {
        const char *value = nullptr;
        if (strcmp(args[i], "--help") == 0 || strcmp(args[i], "-h") == 0) {
            printf("%s\n\n  --language smith|c   the language of the program: smith (the default) or c\n"
                   "  <name>               the folder and the program's name; with none, the current folder\n",
                   smith_new_usage);
            exit(0);
        }
        if (strcmp(args[i], "--language") == 0) {
            if (i + 1 >= count) smith_die(smith_exit_usage, "--language needs a value: smith or c", smith_new_usage);
            value = args[++i];
        } else if (strncmp(args[i], "--language=", 11) == 0) {
            value = args[i] + 11;
        }
        if (value == nullptr) {
            if (name != nullptr) smith_die(smith_exit_usage, "new takes at most one name", smith_new_usage);
            name = args[i];
        } else if (strcmp(value, "smith") == 0) {
            *language = SmithNewLanguage_Smith;
        } else if (strcmp(value, "c") == 0) {
            *language = SmithNewLanguage_C;
        } else {
            smith_die(smith_exit_usage, "--language is smith or c, not", value);
        }
    }
    return name;
}

static int smith_new_main(int count, char **args, const char *self) {
    SMITH_ASSERT(count >= 0);
    SmithNewLanguage language = SmithNewLanguage_Smith;
    const char *argument = smith_new_parse(count, args, &language);
    char folder_name[PATH_MAX];
    const char *name = smith_new_enter(argument, folder_name);
    printf("smith new: %s\n", name);
    bool has_build = smith_new_exists("build.c") || smith_new_exists("build.smith");
    const char *other = has_build ? nullptr : smith_new_other_build();
    bool had_main = smith_new_exists("main.c") || smith_new_exists("main.smith");
    bool had_gitignore = smith_new_exists(".gitignore");
    bool had_settings = smith_new_exists(".claude/settings.json");
    SmithNewCounts counts = {};
    // A main in the other language is the project's own program.
    bool own_main = had_main && !smith_new_exists(language == SmithNewLanguage_Smith ? "main.smith" : "main.c");
    bool skip_main = has_build || other != nullptr || own_main;
    smith_new_files_write(name, language, skip_main, skip_main || had_main, &counts);
    fflush(stdout);
    // The program is not built beside another build system, which the project builds with.
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
        printf("\nThis project builds with a %s, so smith new wrote no program and did not build. To build it with\n"
               "smith, write a build.c that does what the %s does, following the smith-build skill, and remove the\n"
               "%s once smith build gives the same results.\n",
               other, other, other);
        return 0;
    }
    printf("\nNext:\n");
    if (argument != nullptr) printf("  cd %s\n", name);
    printf("  smith run\n");
    return 0;
}

// `smith fmt`, `smith lint`, `smith lsp`, `smith dbg` and `smith dap` are clang-format, clang-tidy, clangd, lldb and
// lldb-dap, with the arguments unchanged, so a project's .clang-format and .clang-tidy apply as they do to the tools
// alone. They are recognised only as the exact first argument. A smith with clang inside runs the tool's own main in
// this process, from the same LLVM as the compiler; a caller who sets the tool's variable runs an external
// tool instead.
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
#else
// lldb on Linux debugs a process through lldb-server, which is this smith (a clone named lldb-server in its programs
// folder, shim/lldb_server.cpp). The caller's LLDB_DEBUGSERVER_PATH wins.
static void smith_debugserver_environment(const char *self) {
    if (getenv("LLDB_DEBUGSERVER_PATH") != nullptr || !smith_has_lldb) return;
    char folder[PATH_MAX];
    char programs[PATH_MAX];
    char server[PATH_MAX];
    smith_resource_folder(folder);
    smith_programs_folder(folder, programs);
    int written = snprintf(server, sizeof server, "%s/bin/lldb-server", programs);
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof server); // smith_programs_folder leaves room for it
    smith_resource_ensure(folder, programs, self);
    setenv("LLDB_DEBUGSERVER_PATH", server, 0);
}
#endif

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

// ---- smith's defaults for a project with no config file (issue #43, part 1)
//
// `smith fmt` and `smith lint` use smith's own .clang-format and .clang-tidy (templates/defaults, embedded) for a file
// the tool would find none for: clang-format's lookup (`.clang-format` or `_clang-format`, in the file's folder and
// each folder above) and clang-tidy's (`.clang-tidy`, the same walk). They are written to <data>/smith/defaults/ (named
// by their content) and passed as `--style=file:<path>` and `--config-file=<path>`. A project's file replaces smith's
// whole, and an explicit option of the tool wins. `--dump-config` is the tool's own, so it prints the effective one.
// `smith lsp` points clangd at `.smith/compile_commands.json` when the project has no root one.
constexpr int smith_defaults_args_max = 4096;
constexpr int smith_defaults_depth_max = 256;

// Whether the folder or one above it holds a file of one of the names (null-terminated list). A path that cannot be
// held counts as found: the tool is then left alone.
static bool smith_defaults_found(const char *start, const char *const *names) {
    char folder[PATH_MAX];
    int written = snprintf(folder, sizeof folder, "%s", start);
    if (written <= 0 || (size_t)written >= sizeof folder) return true;
    for (int depth = 0; depth < smith_defaults_depth_max; depth++) {
        for (size_t i = 0; names[i] != nullptr; i++) {
            char path[PATH_MAX + 32];
            snprintf(path, sizeof path, "%s/%s", folder, names[i]);
            struct stat info;
            if (stat(path, &info) == 0 && !S_ISDIR(info.st_mode)) return true;
        }
        char *slash = strrchr(folder, '/');
        if (slash == nullptr || slash == folder) return false;
        *slash = '\0';
    }
    return true;
}

// The folder a file's lookup starts in: its own, made absolute (a file that does not exist yet, or standard input, is
// looked up from where it would be). A null name, "-" and "<stdin>" are the current folder.
static void smith_defaults_start(const char *name, char *folder) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd) == nullptr) cwd[0] = '\0';
    char full[PATH_MAX * 2];
    if (name == nullptr || strcmp(name, "-") == 0 || strcmp(name, "<stdin>") == 0) {
        snprintf(full, sizeof full, "%s/x", cwd);
    } else if (name[0] == '/') {
        snprintf(full, sizeof full, "%s", name);
    } else {
        snprintf(full, sizeof full, "%s/%s", cwd, name);
    }
    char *slash = strrchr(full, '/');
    if (slash != nullptr && slash != full) *slash = '\0';
    char real[PATH_MAX];
    snprintf(folder, PATH_MAX, "%s", realpath(full, real) != nullptr ? real : full);
}

// The path of smith's default file of this kind, written if it is not there (named by its content, so a new default
// is a new file and an older smith's is left alone).
static void smith_defaults_path(const unsigned char *bytes, size_t size, const char *stem, char *path) {
    const char *data = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    char base[PATH_MAX];
    if (data != nullptr && data[0] == '/') {
        snprintf(base, sizeof base, "%s", data);
    } else if (home != nullptr && home[0] == '/') {
        snprintf(base, sizeof base, "%s/.local/share", home);
    } else {
        smith_die(smith_exit_usage, "cannot find a folder for smith's defaults: set HOME or XDG_DATA_HOME", nullptr);
    }
    uint64_t key = 14695981039346656037ULL;
    for (size_t i = 0; i < size; i++)
        key = (key ^ bytes[i]) * 1099511628211ULL; // FNV-1a
    char folder[PATH_MAX];
    for (char *slash = strchr(base + 1, '/'); slash != nullptr;
         slash = strchr(slash + 1, '/')) { // a fresh HOME has no .local
        *slash = '\0';
        mkdir(base, 0777);
        *slash = '/';
    }
    mkdir(base, 0777);
    snprintf(folder, sizeof folder, "%s/smith", base);
    smith_make_folder(folder);
    snprintf(folder, sizeof folder, "%s/smith/defaults", base);
    smith_make_folder(folder);
    int written = snprintf(path, PATH_MAX, "%s/%s-%016" PRIx64, folder, stem, key);
    SMITH_ASSERT(written > 0 && (size_t)written < PATH_MAX);
    smith_write_if_changed(path, bytes, size);
}

// The option as written: -name, --name, with or without =value.
static bool smith_defaults_is(const char *arg, const char *name) {
    if (arg[0] != '-') return false;
    arg += 1 + (arg[1] == '-');
    size_t length = strlen(name);
    return strncmp(arg, name, length) == 0 && (arg[length] == '\0' || arg[length] == '=');
}

// Whether the option is one that takes its value from the next argument when written without `=`.
static bool smith_defaults_takes_value(const char *arg, const char *const *names) {
    if (arg[0] != '-' || strchr(arg, '=') != nullptr) return false;
    for (size_t i = 0; names[i] != nullptr; i++)
        if (smith_defaults_is(arg, names[i])) return true;
    return false;
}

// Runs this smith on the arguments and returns its exit status.
static int smith_defaults_again(char *const *args) {
    pid_t pid;
    if (posix_spawn(&pid, args[0], nullptr, nullptr, args, environ) != 0) return smith_exit_missing;
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return smith_exit_missing;
    return WIFEXITED(status) ? WEXITSTATUS(status) : smith_exit_missing;
}

typedef struct {
    int files[smith_defaults_args_max]; // the indexes of the file arguments
    bool missing[smith_defaults_args_max];
    int count;
    int missing_count;
    bool explicit_option; // the user chose a style or config
    const char *assumed;  // --assume-filename
} SmithDefaultsScan;

static SmithDefaultsScan smith_defaults_scan;

// Reads the tool's arguments (argv[2 ..]): which are files, whether the user chose a style or config, and which files
// have no config file in the project.
static void smith_defaults_read(bool fmt, int argc, char **argv) {
    static const char *const fmt_names[] = {".clang-format", "_clang-format", nullptr};
    static const char *const tidy_names[] = {".clang-tidy", nullptr};
    static const char *const fmt_valued[] = {"style",  "assume-filename", "lines",  "offset",
                                             "length", "fallback-style",  "cursor", nullptr};
    static const char *const tidy_valued[] = {"p",
                                              "checks",
                                              "config",
                                              "config-file",
                                              "header-filter",
                                              "format-style",
                                              "export-fixes",
                                              "extra-arg",
                                              "extra-arg-before",
                                              "line-filter",
                                              "load",
                                              "vfsoverlay",
                                              "store-check-profile",
                                              nullptr};
    SmithDefaultsScan *scan = &smith_defaults_scan;
    *scan = (SmithDefaultsScan){};
    for (int i = 2; i < argc; i++) { // bounded by the arguments
        const char *arg = argv[i];
        if (strcmp(arg, "--") == 0) break; // clang-tidy's compile arguments follow
        if (arg[0] == '-' && arg[1] != '\0') {
            if (fmt && (smith_defaults_is(arg, "style") || smith_defaults_is(arg, "fallback-style")))
                scan->explicit_option = true;
            if (!fmt && (smith_defaults_is(arg, "config") || smith_defaults_is(arg, "config-file")))
                scan->explicit_option = true;
            if (fmt && smith_defaults_is(arg, "assume-filename")) {
                const char *equals = strchr(arg, '=');
                scan->assumed = equals != nullptr ? equals + 1 : (i + 1 < argc ? argv[i + 1] : nullptr);
            }
            if (smith_defaults_takes_value(arg, fmt ? fmt_valued : tidy_valued) && i + 1 < argc) i++;
            continue;
        }
        scan->files[scan->count++] = i;
    }
    int checked = scan->count > 0 ? scan->count : 1; // no file: standard input, or the current folder
    for (int k = 0; k < checked; k++) {
        const char *name = scan->count > 0 ? argv[scan->files[k]] : nullptr;
        if (scan->assumed != nullptr && (name == nullptr || strcmp(name, "-") == 0)) name = scan->assumed;
        char start[PATH_MAX];
        smith_defaults_start(name, start);
        scan->missing[k] = !smith_defaults_found(start, fmt ? fmt_names : tidy_names);
        scan->missing_count += scan->missing[k] ? 1 : 0;
    }
}

// The extensions of the files `smith fmt` formats: clang-format reads any other file as C and corrupts it (a shell
// script rewritten as C was the case that made this a rule).
static const char *const smith_fmt_extensions[] = {".c",   ".h",   ".m",  ".mm",    ".cpp",   ".cc",
                                                   ".cxx", ".hpp", ".hh", ".smith", ".metal", nullptr};

static bool smith_fmt_formats(const char *file) {
    const char *dot = strrchr(file, '.');
    const char *slash = strrchr(file, '/');
    if (dot == nullptr || (slash != nullptr && dot < slash)) return false;
    for (size_t i = 0; smith_fmt_extensions[i] != nullptr; i++)
        if (strcmp(dot, smith_fmt_extensions[i]) == 0) return true;
    return false;
}

// A file named on the command line that is not C, with no --assume-filename to say what it is: an error that names the
// file and the extensions, and nothing is written. Standard input ("-") and a name that is no file are clang-format's.
static void smith_fmt_refuse_non_c(const SmithDefaultsScan *scan, char **args) {
    if (scan->assumed != nullptr) return;
    for (int k = 0; k < scan->count; k++) {
        const char *file = args[scan->files[k]];
        struct stat info;
        if (strcmp(file, "-") == 0 || smith_fmt_formats(file) || stat(file, &info) != 0 || !S_ISREG(info.st_mode))
            continue;
        fprintf(stderr, "smith: fmt: %s is not a C file, and nothing was written; smith fmt formats", file);
        for (size_t i = 0; smith_fmt_extensions[i] != nullptr; i++)
            fprintf(stderr, " %s", smith_fmt_extensions[i]);
        fprintf(stderr, " (--assume-filename=x.c formats it as that)\n");
        exit(smith_exit_usage);
    }
}

static char *smith_defaults_argv[smith_defaults_args_max];
static char smith_defaults_option[PATH_MAX + 32];

// Whether argument i is a file argument that has a config file of the project's (keep) or none (default).
static bool smith_defaults_is_file(int i, bool want_missing) {
    SmithDefaultsScan *scan = &smith_defaults_scan;
    for (int k = 0; k < scan->count; k++)
        if (scan->files[k] == i) return scan->missing[k] == want_missing;
    return false;
}

// For `smith fmt` and `smith lint` with the tool's arguments (argv[1] the command): the arguments with smith's default
// added for the files that have no config file of the project's, in a static array (the tool runs once per process).
// Files with a config file and files without need different options, so when both are named the second kind runs in
// a process of its own. Returns the exit status of that process, or -1 when there was none.
static int smith_defaults_apply(const SmithTool *tool, int *argc, char ***argv, const char *self) {
    bool fmt = strcmp(tool->command, "fmt") == 0;
    int count = *argc;
    char **args = *argv;
    if (count > smith_defaults_args_max - 8) return -1;
    smith_defaults_read(fmt, count, args);
    SmithDefaultsScan *scan = &smith_defaults_scan;
    if (fmt) smith_fmt_refuse_non_c(scan, args);
    if (scan->explicit_option || scan->missing_count == 0) return -1;
    char path[PATH_MAX];
    if (fmt)
        smith_defaults_path(smith_defaults_clang_format, sizeof smith_defaults_clang_format, "clang-format", path);
    else
        smith_defaults_path(smith_defaults_clang_tidy, sizeof smith_defaults_clang_tidy, "clang-tidy", path);
    int written = snprintf(smith_defaults_option, sizeof smith_defaults_option,
                           fmt ? "--style=file:%s" : "--config-file=%s", path);
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof smith_defaults_option);
    bool all = scan->count == 0 || scan->missing_count == scan->count;
    int status = -1;
    if (!all) { // the files without run in a process of their own, with the default
        char *child[smith_defaults_args_max];
        int used = 0;
        child[used++] = (char *)self;
        child[used++] = args[1];
        child[used++] = smith_defaults_option;
        for (int i = 2; i < count; i++)
            if (!smith_defaults_is_file(i, false)) child[used++] = args[i];
        child[used] = nullptr;
        status = smith_defaults_again(child);
    }
    int out = 0;
    smith_defaults_argv[out++] = args[0];
    smith_defaults_argv[out++] = args[1];
    if (all) smith_defaults_argv[out++] = smith_defaults_option;
    for (int i = 2; i < count; i++)
        if (all || !smith_defaults_is_file(i, true)) smith_defaults_argv[out++] = args[i];
    smith_defaults_argv[out] = nullptr;
    *argc = out;
    *argv = smith_defaults_argv;
    return status;
}

// `smith lsp` in a project that has `.smith/compile_commands.json` and no compile database of its own at its root (or
// in build/, clangd's own places): the option that makes clangd read that one. The project is the nearest folder, up
// from the current one, that has the file. clangd has no other way to be told (no LLVM change).
static char smith_lsp_option[PATH_MAX + 40];

static bool smith_lsp_commands_option(int argc, char **argv) {
    for (int i = 2; i < argc; i++)
        if (smith_defaults_is(argv[i], "compile-commands-dir")) return false;
    char folder[PATH_MAX];
    if (getcwd(folder, sizeof folder) == nullptr) return false;
    for (int depth = 0; depth < smith_defaults_depth_max; depth++) {
        char path[PATH_MAX + 64];
        struct stat info;
        snprintf(path, sizeof path, "%s/.smith/compile_commands.json", folder);
        if (stat(path, &info) == 0) {
            snprintf(path, sizeof path, "%s/compile_commands.json", folder);
            if (stat(path, &info) == 0) return false;
            snprintf(path, sizeof path, "%s/build/compile_commands.json", folder);
            if (stat(path, &info) == 0) return false;
            int written =
                snprintf(smith_lsp_option, sizeof smith_lsp_option, "--compile-commands-dir=%s/.smith", folder);
            return written > 0 && (size_t)written < sizeof smith_lsp_option;
        }
        char *slash = strrchr(folder, '/');
        if (slash == nullptr || slash == folder) return false;
        *slash = '\0';
    }
    return false;
}

// `smith lsp` writes the libraries' headers that a project's .smith/include lacks (a fresh clone, an emptied .smith/),
// from the sources embedded in this binary: one module's tokens each, no compile, so an editor finds <smith/arena.h>
// and `import smith::arena` before the first `smith build`. The project is the nearest folder, up from the current one,
// that has a build file or a .smith/ folder; a folder with neither is no project and gets nothing.
static void smith_lsp_headers(const char *self) {
    char start[PATH_MAX];
    char folder[PATH_MAX];
    if (getcwd(start, sizeof start) == nullptr) return;
    snprintf(folder, sizeof folder, "%s", start);
    for (int depth = 0; depth < smith_defaults_depth_max; depth++) {
        static const char *const marks[] = {"/build.c", "/build.smith", "/.smith"};
        bool project = false;
        for (size_t i = 0; i < sizeof marks / sizeof marks[0]; i++) {
            char path[PATH_MAX + 16];
            snprintf(path, sizeof path, "%s%s", folder, marks[i]);
            project = project || access(path, F_OK) == 0;
        }
        if (project) {
            if (chdir(folder) != 0) return;
            smith_write_embedded_headers(self);
            if (chdir(start) != 0) smith_die(smith_exit_missing, "cannot return to", start);
            return;
        }
        char *slash = strrchr(folder, '/');
        if (slash == nullptr || slash == folder) return;
        *slash = '\0';
    }
}

// `smith lsp`, in process: clangd formats with smith's style and runs its checks by smith's tidy config where the
// project has no .clang-format or .clang-tidy, as `smith fmt` and `smith lint` do
// (patches/smith-editor-defaults.patch). A project's file wins whole, and clangd's own --fallback-style wins over
// smith's style.
static void smith_lsp_defaults(void) {
    static char style[PATH_MAX];
    static char tidy[PATH_MAX];
    smith_defaults_path(smith_defaults_clang_format, sizeof smith_defaults_clang_format, "clang-format", style);
    smith_defaults_path(smith_defaults_clang_tidy, sizeof smith_defaults_clang_tidy, "clang-tidy", tidy);
    smith_clangd_defaults(style, tidy);
}

// argv is main's, with argv[1] the command; the tool gets argv[1 ..] with the command replaced by its own path, so its
// arguments are unchanged.
static int smith_tool_launch(const SmithTool *tool, int argc, char **argv, const char *self) {
    SMITH_ASSERT(tool != nullptr && argc >= 2 && argv[0] != nullptr);
#ifdef __APPLE__
    smith_sdk_environment(self); // clangd, and clang-tidy's compiles, find the SDK as the compiler does
    if (strcmp(tool->command, "dbg") == 0 || strcmp(tool->command, "dap") == 0) smith_debugserver_environment();
#else
    if (strcmp(tool->command, "dbg") == 0 || strcmp(tool->command, "dap") == 0) smith_debugserver_environment(self);
#endif
    bool crash = strcmp(tool->command, "dbg") == 0 && argc >= 3 &&
                 (strcmp(argv[2], "--crash") == 0 || strcmp(argv[2], "--at") == 0);
    if (crash) return smith_crash_main(argc - 2, argv + 2, self); // runtime/explain.c, on the code of a failed check
    const char *override = getenv(tool->override);
    bool debugger = strcmp(tool->command, "dbg") == 0 || strcmp(tool->command, "dap") == 0;
    if ((override == nullptr || *override == '\0') && (!debugger || smith_has_lldb)) {
        if (strcmp(tool->command, "lsp") == 0) {
            smith_lsp_headers(self);
            smith_lsp_defaults();
        }
        return smith_tool_in_process(tool, argc - 1, argv + 1, self);
    }
    char program[PATH_MAX];
    smith_find_clang(tool->tool, tool->override, self, program);
    argv[1] = program; // the full path, as for the compiler
    smith_run(program, argv + 1);
    return smith_exit_missing;
}

// `smith fmt <files>` rewrites the files, as `smith fmt` does: clang-format's own default prints the formatted text of
// every file to standard output, which no one wants of a file they named (agents lost their context to it). Any
// option that chooses another mode (-n, --dry-run, --lines, --assume-filename, --output-replacements-xml, ...) keeps
// clang-format's meaning, and so does `smith fmt < x.c`, which names no file. Adds -i to the arguments when it applies.
static char *smith_fmt_rewrite_argv[smith_defaults_args_max];

static bool smith_fmt_names_a_file_only(int argc, char **argv) {
    static const char *const modes[] = {"i",
                                        "n",
                                        "dry-run",
                                        "lines",
                                        "offset",
                                        "length",
                                        "cursor",
                                        "assume-filename",
                                        "dump-config",
                                        "list-ignored",
                                        "output-replacements-xml",
                                        "files",
                                        "version",
                                        "help",
                                        "h",
                                        "print-config",
                                        nullptr};
    static const char *const valued[] = {"style", "fallback-style", nullptr};
    bool file = false;
    for (int i = 2; i < argc; i++) { // bounded by the arguments
        const char *arg = argv[i];
        if (arg[0] != '-' || arg[1] == '\0') {
            file = file || arg[0] != '-';
            continue;
        }
        for (size_t k = 0; modes[k] != nullptr; k++)
            if (smith_defaults_is(arg, modes[k])) return false;
        if (smith_defaults_takes_value(arg, valued) && i + 1 < argc) i++;
    }
    return file;
}

static void smith_fmt_rewrite(int *argc, char ***argv) {
    if (*argc >= smith_defaults_args_max - 2 || !smith_fmt_names_a_file_only(*argc, *argv)) return;
    int out = 0;
    smith_fmt_rewrite_argv[out++] = (*argv)[0];
    smith_fmt_rewrite_argv[out++] = (*argv)[1];
    smith_fmt_rewrite_argv[out++] = (char *)"-i";
    for (int i = 2; i < *argc; i++)
        smith_fmt_rewrite_argv[out++] = (*argv)[i];
    smith_fmt_rewrite_argv[out] = nullptr;
    *argc = out;
    *argv = smith_fmt_rewrite_argv;
}

// The tool, after smith's defaults: its arguments with smith's style or tidy config where the project has none
// (`smith fmt`, `smith lint`), or with the compile commands of .smith/ (`smith lsp`).
static int smith_tool_main(const SmithTool *tool, int argc, char **argv, const char *self) {
    SMITH_ASSERT(tool != nullptr && argc >= 2 && argv[0] != nullptr);
    int before = -1;
    if (strcmp(tool->command, "fmt") == 0) smith_fmt_rewrite(&argc, &argv);
    if (strcmp(tool->command, "fmt") == 0 || strcmp(tool->command, "lint") == 0) {
        before = smith_defaults_apply(tool, &argc, &argv, self);
    } else if (strcmp(tool->command, "lsp") == 0 && argc < smith_defaults_args_max - 2 &&
               smith_lsp_commands_option(argc, argv)) {
        for (int i = 0; i < argc; i++)
            smith_defaults_argv[i] = argv[i];
        smith_defaults_argv[argc++] = smith_lsp_option;
        smith_defaults_argv[argc] = nullptr;
        argv = smith_defaults_argv;
    }
    int status = smith_tool_launch(tool, argc, argv, self);
    return status != 0 ? status : (before > 0 ? before : 0);
}

// `smith cc --smith-headers [compile flags] -- source header [source header ...]`: `--smith-header` for each pair, in
// this one process (#639). A smith process costs about 10 ms to start (a 240 MB image and 400,000 fixups), the header
// itself about 2, so the build writes the headers of many modules in one step. Each pair runs in a child of its own
// (the lowering keeps its tables in statics), one after the other: a header names the headers of what it imports, so
// they are never written at the same time. The status is 0 when every header was written (only when its bytes change).
static int smith_headers_main(int count, char **args) {
    constexpr int arguments_max = 80;
    const char *usage = "usage: smith cc --smith-headers [compile flags] -- source header [source header ...]";
    int split = 0;
    while (split < count && strcmp(args[split], "--") != 0)
        split++; // bounded by the arguments
    if (split == count || (count - split - 1) % 2 != 0 || split + 3 > arguments_max)
        smith_die(smith_exit_usage, usage, nullptr);
    int failed = 0;
    fflush(nullptr);
    for (int i = split + 1; i < count; i += 2) { // bounded by the arguments
        char *child[arguments_max];
        int used = 0;
        child[used++] = args[i];
        child[used++] = (char *)"-o";
        child[used++] = args[i + 1];
        for (int j = 0; j < split; j++)
            child[used++] = args[j];
        pid_t pid = fork();
        if (pid < 0) smith_die(smith_exit_missing, "cannot fork", args[i]);
        if (pid == 0) _exit(smith_lang_main(used, child, true));
        int status = 0;
        pid_t waited = -1;
        for (int tries = 0; tries < 1000 && waited < 0; tries++)
            waited = waitpid(pid, &status, 0); // bounded; a signal
        if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) failed = 1;
    }
    return failed;
}

// The compiler: `smith cc <args>` and `smith c++ <args>`, and the links smith-cc and smith-c++, which act as them by
// their name. The program is argv[at]; --smith-which, when it comes first, names the compiler without running it,
// and --smith-header, --smith-lower and --smith-tree-sitter are the .smith helpers.
static int smith_compile_main(int argc, char **argv, int at, bool plus, const char *self) {
    SMITH_ASSERT(at >= 0 && at < argc);
    int first = at + 1;
    // The .smith helpers that need no compiler run: --smith-header writes a module's header, --smith-lower prints a
    // file's lowering (knowledge/smith-language.md).
    if (first < argc && strcmp(argv[first], "--smith-header") == 0)
        return smith_lang_main(argc - first - 1, argv + first + 1, true);
    if (first < argc && strcmp(argv[first], "--smith-headers") == 0)
        return smith_headers_main(argc - first - 1, argv + first + 1);
    if (first < argc && strcmp(argv[first], "--smith-lower") == 0)
        return smith_lang_main(argc - first - 1, argv + first + 1, false);
    if (first < argc && strcmp(argv[first], "--smith-tree-sitter") == 0) return smith_tree_sitter_main(self);
#ifdef SMITH_HOOK
    if (first < argc && strcmp(argv[first], "--smith-effects") == 0)
        return smith_effects_check(argc - first - 1, argv + first + 1, nullptr);
#endif
    bool which = first < argc && strcmp(argv[first], "--smith-which") == 0;
    if (which) first++;
    int count = argc - first;
    char **args = argv + first;
#ifdef SMITH_HOOK
    if (!which) {
        int status = smith_check_effects(count, args);
        if (status > 0) return status;
        if (status == 0) setenv("SMITH_EFFECTS_CHECKED", "1", 1); // the linker the driver starts need not run it again
    }
#endif
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

// Whether every argument is a -D option of the build, which neither clang-format nor clang-tidy has (none is true).
static bool smith_only_options(int count, char **args) {
    for (int i = 0; i < count; i++) { // bounded by count
        if (strncmp(args[i], "-D", 2) != 0) return false;
    }
    return true;
}

// Whether the arguments of a verb that is the tool with arguments are only -D options of the build and the few options
// of the tool that the project's action takes too: `smith fmt --dry-run` is the project's dry run, not clang-format
// waiting on standard input for a file; `smith lint --quiet` is the project's lint. Other options (and any file name)
// leave the verb the tool, unchanged (`smith fmt --assume-filename=x.c` reads standard input).
static bool smith_project_arguments(const char *verb, int count, char **args) {
    const char *const *own = nullptr;
    static const char *const fmt_options[] = {"--dry-run", "-n", "--Werror", "-i", nullptr};
    static const char *const lint_options[] = {"--quiet", nullptr};
    static const char *const prove_options[] = {"-v", "--no-cache", "--induction", "--unroll", nullptr};
    if (strcmp(verb, "fmt") == 0) own = fmt_options;
    if (strcmp(verb, "lint") == 0) own = lint_options;
    if (strcmp(verb, "prove") == 0) own = prove_options;
    for (int i = 0; i < count; i++) { // bounded by count
        if (strncmp(args[i], "-D", 2) == 0) continue;
        // `smith prove --budget=20`, `--emit-ir=ir.ll`, `--induction-from=8` and `--function=name` are the project's
        // too: they name no file
        if (strcmp(verb, "prove") == 0 &&
            (strncmp(args[i], "--budget=", 9) == 0 || strncmp(args[i], "--emit-ir=", 10) == 0 ||
             strncmp(args[i], "--induction-from=", 17) == 0 || strncmp(args[i], "--function=", 11) == 0))
            continue;
        bool known = false;
        for (size_t j = 0; own != nullptr && own[j] != nullptr; j++)
            known = known || strcmp(args[i], own[j]) == 0;
        if (!known) return false;
    }
    return true;
}

// `smith docs`: the check of AGENTS.md, knowledge/ and the skills, with the types knowledge/index.md declares.
#include "runtime/docs.c"

// `smith mcp`: the debugger server of issue #22. A feature of the full smith (-DSMITH_MCP, which `smith build` passes
// with clang inside), as the prover and the debugger are: a smith built without it has the verb and says so. It stands
// on the JSON, text and protocol code (knowledge/protocol-generation.md; text is the module smith::text, linked as its
// own object), and on the io module.
#ifdef SMITH_MCP
#include "dap.c"
#include "lib/json.c"
#include "mcp.c"
#include "runtime/mcp.c"
#include "runtime/protocol/protocol.c"
#endif

// The verbs that act on a project by what it declares (fmt, lint, prove, test, run, check).
// The tests a change can affect: `smith test --changed` (issue #441).
// The coverage table it reads comes first (issue #456).
#include "runtime/coverage.c"
#include "runtime/record.c"

#include "runtime/changed.c"
#include "runtime/verbs.c"

// smith's top level is only verbs. This table is where they are: a new verb is a row and, when it is none of these
// kinds, a case in smith_dispatch. A verb whose row says Project, ProjectBare or ToolOrProject does one fixed thing to
// the project (runtime/verbs.c), and `smith build <name>` is refused for it.
typedef enum {
    SmithVerb_Cc,    // the compiler for C, Objective-C and Metal, with clang's arguments
    SmithVerb_Cxx,   // the same for C++
    SmithVerb_Build, // the project's build.c
    SmithVerb_New,
    SmithVerb_Fetch,
    SmithVerb_Cache,
    SmithVerb_Docs,          // smith docs: checks AGENTS.md, knowledge/ and skills
    SmithVerb_Symbolize,     // smith symbolize: stack traces turned into lines
    SmithVerb_Version,       // smith version: the version line and the commit smith was built from
    SmithVerb_Mcp,           // smith mcp: the debugger as an MCP server (the full smith)
    SmithVerb_Tool,          // an LLVM tool, whose arguments are its own (smith_tools)
    SmithVerb_ToolOrProject, // the tool (or smith_prove) with arguments, the fixed action on the project with none
    SmithVerb_Project, // the fixed action on the project; the arguments are the action's (the runner's, the program's)
    SmithVerb_ProjectBare, // the same, with no arguments but -D options
} SmithVerbKind;

typedef struct {
    const char *name;
    SmithVerbKind kind;
    int (*project)(int count, char **args, const char *self); // what the verb does for the project itself
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
    {"fmt", SmithVerb_ToolOrProject, smith_fmt_verb},
    {"lint", SmithVerb_ToolOrProject, smith_lint_project},
    {"lsp", SmithVerb_Tool, nullptr},
    {"run", SmithVerb_Project, smith_run_project},
    {"test", SmithVerb_Project, smith_test_project},
    {"check", SmithVerb_ProjectBare, smith_check_project},
    {"prove", SmithVerb_ToolOrProject, smith_prove_project},
    {"dbg", SmithVerb_Tool, nullptr},
    {"dap", SmithVerb_Tool, nullptr},
    {"mcp", SmithVerb_Mcp, nullptr},
};

static const SmithVerb *smith_find_verb(const char *name) {
    for (size_t i = 0; i < sizeof smith_verbs / sizeof smith_verbs[0]; i++) {
        if (strcmp(name, smith_verbs[i].name) == 0) return &smith_verbs[i];
    }
    return nullptr;
}

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
    printf("smith 0.0%s%s\n", known ? " commit " : "", smith_commit);
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
    case SmithVerb_Mcp:
#ifdef SMITH_MCP
        return smith_mcp_main(argc - 2, argv + 2, self);
#else
        fprintf(stderr, "smith: mcp needs the full smith (clang inside); this one was built without it "
                        "(knowledge/mcp-server.md)\n");
        return smith_exit_missing;
#endif
    case SmithVerb_Docs:
        return smith_docs_main(argc - 2, argv + 2);
    case SmithVerb_Symbolize:
        return smith_symbolize_main(argc - 2, argv + 2);
    case SmithVerb_Version:
        return smith_version_main();
    case SmithVerb_ToolOrProject: // with arguments it is the tool, with none (or only -D options) the project's
        if (!smith_project_arguments(verb->name, argc - 2, argv + 2)) {
            if (strcmp(verb->name, "prove") == 0) return smith_prove(argc - 2, argv + 2, self);
            return smith_tool_main(smith_find_tool(verb->name), argc, argv, self);
        }
        return verb->project(argc - 2, argv + 2, self);
    case SmithVerb_Project:
    case SmithVerb_ProjectBare:
        return verb->project(argc - 2, argv + 2, self);
    }
    return smith_exit_missing;
}

static void smith_usage(FILE *out) {
    fputs("usage: smith <command> [<arguments>]\n"
          "  cc, c++                 compile and link as clang does (the links smith-cc and smith-c++ do the same)\n"
          "  build [<target>]        build the project from its build.c\n"
          "  run, test, check        run the project's program, its tests, or all its checks\n"
          "  fmt, lint               with no arguments, format or lint the project; with arguments, clang-format and\n"
          "                          clang-tidy (`fmt <files>` rewrites them in place)\n"
          "  lsp                     clangd\n"
          "  dbg, dap                lldb, and lldb-dap (the Debug Adapter Protocol server)\n"
          "  mcp                     the debugger as an MCP server over stdio (agents: launch, breakpoint, stack...)\n"
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
    smith_account_start();
    char self[PATH_MAX];
    if (!smith_self_path(self)) smith_die(smith_exit_missing, "cannot determine the path of this executable", nullptr);

    const char *name = smith_base_name(argv[0]);
    if (strcmp(name, "ld") == 0) return smith_route_linker(argc, argv);
    if (strcmp(name, "lldb-argdumper") == 0) return smith_argdumper(argc, argv);
#if defined(__linux__) && defined(SMITH_LLDB)
    if (strcmp(name, "lldb-server") == 0) return smith_lldb_server_main(argc, argv);
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
    if (strcmp(argv[1], "--smith-explain") == 0) {
#ifdef __APPLE__
        smith_debugserver_environment();
#else
        smith_debugserver_environment(self);
#endif
        return smith_explain_main(argc - 2, argv + 2, self);
    }
    if (strcmp(argv[1], "--smith-exec") == 0) {
        smith_slots_arm("exec"); // the first real step takes the slot, as in a build
        return smith_exec_main(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "--smith-account") == 0) return smith_account_main(argc - 2, argv + 2);
    if (strcmp(argv[1], "--smith-slots") == 0) return smith_slots_main(argc - 2, argv + 2);
    const SmithVerb *verb = smith_find_verb(argv[1]);
    if (verb == nullptr) smith_bare_arguments(argc, argv);
    if (smith_slots_verb(verb->name)) smith_slots_arm(verb->name);
    return smith_dispatch(verb, argc, argv, self);
}
