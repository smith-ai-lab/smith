// Builds smith's pinned LLVM on Linux, static, from source: what scripts/llvm.sh does on macOS, for ELF (issue #305).
// This file is plain C23: the seed (scripts/stage0.c on a machine with no smith) builds
// it with the system's C compiler, and `smith build llvm` with smith. It runs from the repository's root:
//   llvm_linux <llvm source> <cmake> <python> <stamp> <ninja>
// the same arguments as scripts/llvm.sh, with <cmake> and <python> the programs themselves and <ninja> the ninja to
// run (an empty argument is the one on PATH). The work lives in the package cache, $CACHE/smith/llvm/<name>/{src,
// build,install}, which `smith build clean` never touches; run again with nothing changed, cmake and ninja find nothing
// to do. The steps:
//   1. the patches (patches/*.patch) go into a copy of the sources, as in scripts/llvm.sh: a patch that changed is
//      taken out with those after it, and only the files they touch change, so the rebuild is incremental;
//   2. CMake and ninja build and install LLVM, clang, clang-tools-extra, lld, lldb and compiler-rt, with the host's
//      target (X86 or AArch64), RTTI and exceptions on (Alive2 needs them), static libraries only;
//   3. what smith links that LLVM does not install: libclangmain.a (clang's main as a library), libsmithtools.a
//      (clang-format's, clang-tidy's and clangd's mains, their static initializers moved out of .init_array into
//      __smith_<tool>, which shim/platform_linux.h runs when the tool runs) and lldb's static archives (the same for
//      __smith_lldb);
//   4. the prover (issue #820): Z3, Alive2's libraries and Bitwuzla with GMP and MPFR into <LLVM folder>/prove/{lib,
//      include}, Z3's static initializers deferred to __smith_prove; and Enzyme into
//      $CACHE/smith/enzyme/0.0.303-<name>; all from the packages build.c pins (the same URLs and hashes), by the host's
//      compiler;
//   5. the identity, lib/smith-clang.id: "llvm-23.1.3-linux " and the SHA-256 of what smith links.
#if defined(__linux__)
#define _GNU_SOURCE // glibc declares setenv and chdir only for X/Open or GNU code, not plain -std=c23
#endif
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include "sha256.h"

extern char **environ;

constexpr size_t path_max = 4096;     // PATH_MAX of Linux, which strict C23 does not define
constexpr size_t arguments_max = 256; // a command: cmake's options, or the objects of one archive
constexpr size_t file_max = 1 << 20;  // a patch, read whole to compare it

// The folder in the package cache; SMITH_LLVM_NAME builds into another (as in scripts/llvm.sh and build.c).
static char cache[path_max];
static char tools[path_max]; // cache/install/bin, where llvm-ar and llvm-objcopy are once LLVM is installed

[[noreturn]] static void die(const char *message, const char *detail) {
    fprintf(stderr, "llvm_linux: %s%s%s\n", message, detail != nullptr ? " " : "", detail != nullptr ? detail : "");
    exit(1);
}

// Runs a program (found on PATH) and waits; true when it exits 0. `out` is a descriptor for its standard output, or -1
// to share ours.
static bool run_to(const char *const *argv, int out) {
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (out >= 0) posix_spawn_file_actions_adddup2(&actions, out, 1);
    pid_t child = 0;
    int started = posix_spawnp(&child, argv[0], &actions, nullptr, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (started != 0) return false;
    int status = 0;
    while (waitpid(child, &status, 0) < 0) { // only a signal interrupts the wait
        if (errno != EINTR) return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void run(const char *const *argv) {
    if (!run_to(argv, -1)) die("failed:", argv[0]);
}

static void join(char *out, const char *format, const char *a, const char *b) {
    int written = snprintf(out, path_max, format, a, b);
    if (written < 0 || (size_t)written >= path_max) die("a path is too long:", a);
}

// A path for one call: the result lives until 256 more are made, so a path that is kept is join()ed into a buffer of
// its own.
static const char *path(const char *format, const char *a, const char *b) {
    static char paths[256][path_max];
    static size_t next = 0;
    char *out = paths[next++ % 256];
    join(out, format, a, b);
    return out;
}

static const char *tool(const char *name) {
    return path("%s/%s", tools, name);
}

static bool same_file(const char *a, const char *b) {
    static char left[file_max];
    static char right[file_max];
    FILE *first = fopen(a, "rb");
    FILE *second = fopen(b, "rb");
    size_t got_left = first != nullptr ? fread(left, 1, sizeof left, first) : 0;
    size_t got_right = second != nullptr ? fread(right, 1, sizeof right, second) : 0;
    bool same = first != nullptr && second != nullptr && got_left == got_right && memcmp(left, right, got_left) == 0;
    if (first != nullptr) fclose(first);
    if (second != nullptr) fclose(second);
    return same;
}

static const char *base_name(const char *file) {
    const char *slash = strrchr(file, '/');
    return slash != nullptr ? slash + 1 : file;
}

// Step 1. A fresh copy of the sources when there is none, then out with every applied patch from the first that
// differs from patches/ (last first), then in with every patch not applied.
static void apply_patches(const char *source) {
    char src[path_max];
    join(src, "%s/src", cache, "");
    char applied[path_max];
    join(applied, "%s/src/.patches", cache, "");
    struct stat info;
    if (stat(applied, &info) != 0) {
        run((const char *[]){"rm", "-rf", src, nullptr});
        run((const char *[]){"mkdir", "-p", src, nullptr});
        run((const char *[]){"cp", "-R", path("%s/.", source, ""), src, nullptr});
        run((const char *[]){"chmod", "-R", "u+w", src, nullptr});
        run((const char *[]){"mkdir", "-p", applied, nullptr});
    }
    glob_t done = {};
    glob(path("%s/*.patch", applied, ""), 0, nullptr, &done);
    size_t first_stale = done.gl_pathc;
    for (size_t i = 0; i < done.gl_pathc && first_stale == done.gl_pathc; i++) {
        if (!same_file(done.gl_pathv[i], path("patches/%s", base_name(done.gl_pathv[i]), ""))) first_stale = i;
    }
    for (size_t i = done.gl_pathc; i > first_stale; i--) { // last first
        run((const char *[]){"patch", "-R", "-p1", "-s", "-d", src, "-i", done.gl_pathv[i - 1], nullptr});
        run((const char *[]){"rm", done.gl_pathv[i - 1], nullptr});
    }
    globfree(&done);
    glob_t wanted = {};
    glob("patches/*.patch", 0, nullptr, &wanted);
    for (size_t i = 0; i < wanted.gl_pathc; i++) {
        const char *record = path("%s/%s", applied, base_name(wanted.gl_pathv[i]));
        if (stat(record, &info) == 0) continue;
        char here[path_max]; // patch -d changes folder before it opens the patch: its path is absolute
        if (getcwd(here, sizeof here) == nullptr) die("cannot tell the current folder", nullptr);
        run((const char *[]){"patch", "-p1", "-s", "-d", src, "-i", path("%s/%s", here, wanted.gl_pathv[i]), nullptr});
        run((const char *[]){"cp", wanted.gl_pathv[i], record, nullptr});
    }
    globfree(&wanted);
}

// The LLVM target of the machine this runs on.
static const char *host_target(void) {
    struct utsname system;
    if (uname(&system) != 0) die("cannot tell the machine", nullptr);
    if (strcmp(system.machine, "x86_64") == 0) return "X86";
    if (strcmp(system.machine, "aarch64") == 0) return "AArch64";
    die("no LLVM target for this machine:", system.machine);
}

// Step 2: scripts/llvm.sh's configuration, with the host's target and nothing of macOS's (no SDK, no system
// debugserver, no libedit). Release, no assertions, static libraries only; off what would link a library from outside
// the C library (zlib, zstd, libxml2, z3, libedit, terminfo, ffi, curl), and for build time only tests, benchmarks,
// examples, docs, bindings and the compiler-rt parts smith does not use. lld links LLVM itself.
static void configure_and_build(const char *cmake, const char *python, const char *ninja) {
    const char *argv[arguments_max] = {cmake,
                                       "-G",
                                       "Ninja",
                                       "-S",
                                       path("%s/src/llvm", cache, ""),
                                       "-B",
                                       path("%s/build", cache, ""),
                                       "-DCMAKE_BUILD_TYPE=Release",
                                       path("-DCMAKE_INSTALL_PREFIX=%s/install", cache, ""),
                                       path("-DCMAKE_MAKE_PROGRAM=%s", ninja, ""),
                                       path("-DPython3_EXECUTABLE=%s", python, ""),
                                       "-DLLVM_USE_LINKER=lld",
                                       "-DLLVM_PARALLEL_LINK_JOBS=2",
                                       "-DLLVM_ENABLE_PROJECTS=clang;clang-tools-extra;lld;lldb",
                                       "-DLLVM_ENABLE_RUNTIMES=compiler-rt",
                                       path("-DLLVM_TARGETS_TO_BUILD=%s", host_target(), ""),
                                       "-DLLVM_ENABLE_ASSERTIONS=OFF",
                                       "-DLLVM_ENABLE_RTTI=ON",
                                       "-DLLVM_ENABLE_EH=ON",
                                       "-DBUILD_SHARED_LIBS=OFF",
                                       "-DLLVM_BUILD_LLVM_DYLIB=OFF",
                                       "-DLLVM_LINK_LLVM_DYLIB=OFF",
                                       "-DCLANG_LINK_CLANG_DYLIB=OFF",
                                       "-DLLVM_ENABLE_ZLIB=OFF",
                                       "-DLLVM_ENABLE_ZSTD=OFF",
                                       "-DLLVM_ENABLE_LIBXML2=OFF",
                                       "-DLLVM_ENABLE_Z3_SOLVER=OFF",
                                       "-DLLVM_ENABLE_LIBEDIT=OFF",
                                       "-DLLVM_ENABLE_TERMINFO=OFF",
                                       "-DLLVM_ENABLE_FFI=OFF",
                                       "-DLLVM_ENABLE_CURL=OFF",
                                       "-DLLVM_INCLUDE_TESTS=OFF",
                                       "-DLLVM_INCLUDE_BENCHMARKS=OFF",
                                       "-DLLVM_INCLUDE_EXAMPLES=OFF",
                                       "-DLLVM_INCLUDE_DOCS=OFF",
                                       "-DLLVM_ENABLE_BINDINGS=OFF",
                                       "-DCLANG_INCLUDE_TESTS=OFF",
                                       "-DCOMPILER_RT_INCLUDE_TESTS=OFF",
                                       "-DCOMPILER_RT_BUILD_LIBFUZZER=OFF",
                                       "-DCOMPILER_RT_BUILD_MEMPROF=OFF",
                                       "-DCOMPILER_RT_BUILD_XRAY=OFF",
                                       "-DCOMPILER_RT_BUILD_ORC=OFF",
                                       "-DCOMPILER_RT_BUILD_CTX_PROFILE=OFF",
                                       "-DLLDB_ENABLE_PYTHON=OFF",
                                       "-DLLDB_ENABLE_LUA=OFF",
                                       "-DLLDB_ENABLE_CURSES=OFF",
                                       "-DLLDB_ENABLE_LZMA=OFF",
                                       "-DLLDB_ENABLE_LIBXML2=OFF",
                                       "-DLLDB_ENABLE_TREESITTER=OFF",
                                       "-DLLDB_ENABLE_LIBEDIT=OFF",
                                       "-DLLDB_INCLUDE_TESTS=OFF",
                                       nullptr};
    run(argv);
    run((const char *[]){"nice", ninja, "-C", path("%s/build", cache, ""), "install", nullptr});
}

// An object or an archive with its static initializers moved out of .init_array into __smith_<name>, as plain data
// that the linker keeps when its __start_ symbol is named. An archive is rewritten whole: its members may share a
// name (liblldbHost.a has two Host.cpp.o), which extracting them would lose.
static void defer(const char *in, const char *out, const char *name) {
    const char *section = path(".init_array=__smith_%s,alloc,load,data,contents", name, "");
    run((const char *[]){tool("llvm-objcopy"), "--rename-section", section, in, out, nullptr});
}

static void archive(const char *out, const char *pattern) {
    glob_t found = {};
    glob(pattern, 0, nullptr, &found);
    if (found.gl_pathc == 0 || found.gl_pathc + 4 > arguments_max) die("no objects, or too many, in", pattern);
    const char *argv[arguments_max] = {tool("llvm-ar"), "rcs", out};
    for (size_t i = 0; i < found.gl_pathc; i++)
        argv[3 + i] = found.gl_pathv[i];
    argv[3 + found.gl_pathc] = nullptr;
    unlink(out);
    run(argv);
    globfree(&found);
}

static void extract(const char *archive_path, const char *folder, const char *member) {
    run((const char *[]){"mkdir", "-p", folder, nullptr});
    const char *output = path("--output=%s", folder, "");
    if (member == nullptr) {
        run((const char *[]){tool("llvm-ar"), "x", output, archive_path, nullptr});
    } else {
        run((const char *[]){tool("llvm-ar"), "x", output, archive_path, member, nullptr});
    }
}

// clang::GetResourcesPath(char const *, void *) and smith_resource_path(char const *, void *) (shim/tool.h), both
// returning a std::string of libstdc++, whose ABI tag is in the names.
static const char get_resources[] = "_ZN5clang16GetResourcesPathB5cxx11EPKcPv";
static const char smith_resources[] = "_Z19smith_resource_pathB5cxx11PKcPv";

// Step 3, the tools (scripts/llvm.sh says why each is done): clang-format's main renamed, clang-tidy's and clangd's
// mains and clangd's tweaks with their initializers deferred, and the two calls that find the resource directory by
// the executable's path redirected to smith_resource_path.
static void make_tools(void) {
    char build[path_max];
    join(build, "%s/build", cache, "");
    char lib[path_max];
    join(lib, "%s/install/lib", cache, "");
    char work[path_max];
    join(work, "%s/build/smith-tools", cache, "");
    run((const char *[]){"rm", "-rf", work, nullptr});
    run((const char *[]){"mkdir", "-p", path("%s/out", work, ""), nullptr});
    const char *format =
        path("%s/tools/clang/tools/clang-format/CMakeFiles/clang-format.dir/ClangFormat.cpp.o", build, "");
    run((const char *[]){tool("llvm-objcopy"), "--redefine-sym", "main=smith_clang_format_main", format,
                         path("%s/format.o", work, ""), nullptr});
    defer(path("%s/format.o", work, ""), path("%s/out/ClangFormat.o", work, ""), "fmt");
    extract(path("%s/libclangTidyMain.a", lib, ""), path("%s/tidy", work, ""), nullptr);
    defer(path("%s/tidy/ClangTidyMain.cpp.o", work, ""), path("%s/out/ClangTidyMain.o", work, ""), "tidy");
    extract(path("%s/libclangdMain.a", lib, ""), path("%s/clangd", work, ""), nullptr);
    defer(path("%s/clangd/ClangdMain.cpp.o", work, ""), path("%s/out/ClangdMain.o", work, ""), "lsp");
    defer(path("%s/clangd/Check.cpp.o", work, ""), path("%s/out/Check.o", work, ""), "lsp");
    extract(path("%s/libclangDaemonTweaks.a", lib, ""), path("%s/tweaks", work, ""), nullptr);
    glob_t tweaks = {};
    glob(path("%s/tweaks/*.o", work, ""), 0, nullptr, &tweaks);
    for (size_t i = 0; i < tweaks.gl_pathc; i++)
        defer(tweaks.gl_pathv[i], path("%s/out/tweak_%s", work, base_name(tweaks.gl_pathv[i])), "lsp");
    globfree(&tweaks);
    char redirect[path_max];
    join(redirect, "%s=%s", get_resources, smith_resources);
    extract(path("%s/libclangDaemon.a", lib, ""), path("%s/daemon", work, ""), "CompileCommands.cpp.o");
    extract(path("%s/libclangTooling.a", lib, ""), path("%s/tooling", work, ""), "Tooling.cpp.o");
    run((const char *[]){tool("llvm-objcopy"), "--redefine-sym", redirect,
                         path("%s/daemon/CompileCommands.cpp.o", work, ""), path("%s/out/CompileCommands.o", work, ""),
                         nullptr});
    run((const char *[]){tool("llvm-objcopy"), "--redefine-sym", redirect, path("%s/tooling/Tooling.cpp.o", work, ""),
                         path("%s/out/Tooling.o", work, ""), nullptr});
    archive(path("%s/libsmithtools.a", lib, ""), path("%s/out/*.o", work, ""));
}

// Step 3, lldb (scripts/llvm.sh says why): libsmithlldb.a holds the SB API's objects and the three mains renamed, and
// lldb's static archives are copied into the install, all with their initializers deferred to __smith_lldb.
static void make_lldb(void) {
    char lldb[path_max];
    join(lldb, "%s/build/tools/lldb", cache, "");
    char work[path_max];
    join(work, "%s/build/smith-lldb", cache, "");
    run((const char *[]){"rm", "-rf", work, nullptr});
    run((const char *[]){"mkdir", "-p", path("%s/objects", work, ""), path("%s/deferred", work, ""), nullptr});
    run((const char *[]){tool("llvm-objcopy"), "--redefine-sym", "main=smith_lldb_driver_main",
                         path("%s/tools/driver/CMakeFiles/lldb.dir/Driver.cpp.o", lldb, ""),
                         path("%s/objects/Driver.o", work, ""), nullptr});
    run((const char *[]){"cp", path("%s/tools/driver/CMakeFiles/lldb.dir/Platform.cpp.o", lldb, ""),
                         path("%s/objects/Platform.o", work, ""), nullptr});
    run((const char *[]){tool("llvm-objcopy"), "--redefine-sym", "main=smith_lldb_dap_tool_main",
                         path("%s/tools/lldb-dap/tool/CMakeFiles/lldb-dap.dir/lldb-dap.cpp.o", lldb, ""),
                         path("%s/objects/lldb-dap.o", work, ""), nullptr});
    // lldb-server, which lldb starts to debug a process on Linux: smith runs it as a clone named lldb-server.
    char server[path_max];
    join(server, "%s/tools/lldb-server/CMakeFiles/lldb-server.dir", lldb, "");
    run((const char *[]){tool("llvm-objcopy"), "--redefine-sym", "main=smith_lldb_server_tool_main",
                         path("%s/lldb-server.cpp.o", server, ""), path("%s/objects/lldb-server.o", work, ""),
                         nullptr});
    static const char *const server_objects[] = {"LLDBServerUtilities", "lldb-gdbserver", "lldb-platform",
                                                 "SystemInitializerLLGS"};
    for (size_t i = 0; i < sizeof server_objects / sizeof server_objects[0]; i++)
        run((const char *[]){"cp", path("%s/%s.cpp.o", server, server_objects[i]),
                             path("%s/objects/server_%s.o", work, server_objects[i]), nullptr});
    glob_t api = {};
    glob(path("%s/source/API/CMakeFiles/liblldb.dir/*.o", lldb, ""), 0, nullptr, &api);
    for (size_t i = 0; i < api.gl_pathc; i++)
        run((const char *[]){"cp", api.gl_pathv[i], path("%s/objects/api_%s", work, base_name(api.gl_pathv[i])),
                             nullptr});
    globfree(&api);
    glob_t objects = {};
    glob(path("%s/objects/*.o", work, ""), 0, nullptr, &objects);
    for (size_t i = 0; i < objects.gl_pathc; i++)
        defer(objects.gl_pathv[i], path("%s/deferred/%s", work, base_name(objects.gl_pathv[i])), "lldb");
    globfree(&objects);
    archive(path("%s/install/lib/libsmithlldb.a", cache, ""), path("%s/deferred/*.o", work, ""));
    glob_t archives = {};
    glob(path("%s/build/lib/liblldb[A-Z]*.a", cache, ""), 0, nullptr, &archives);
    for (size_t i = 0; i < archives.gl_pathc; i++)
        defer(archives.gl_pathv[i], path("%s/install/lib/%s", cache, base_name(archives.gl_pathv[i])), "lldb");
    globfree(&archives);
}

// clang's main as a library: driver.cpp's objects without clang-driver.cpp's main (scripts/llvm.sh).
static void make_clang_main(void) {
    char objects[path_max];
    join(objects, "%s/build/tools/clang/tools/driver/CMakeFiles/clang.dir", cache, "");
    char out[path_max];
    join(out, "%s/install/lib/libclangmain.a", cache, "");
    unlink(out);
    run((const char *[]){tool("llvm-ar"), "rcs", out, path("%s/driver.cpp.o", objects, ""),
                         path("%s/cc1_main.cpp.o", objects, ""), path("%s/cc1as_main.cpp.o", objects, ""),
                         path("%s/cc1gen_reproducer_main.cpp.o", objects, ""), nullptr});
}

// Adds to the hash the standard output of a program (llvm-ar p: an archive's members, never its bytes, which ninja's
// install rewrites with the time) or, with a nullptr program, a file.
static void hash_output(Sha256 *sha, const char *const *argv) {
    static uint8_t buffer[1 << 16];
    int pipes[2];
    if (pipe(pipes) != 0) die("cannot make a pipe", nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipes[1], 1);
    posix_spawn_file_actions_addclose(&actions, pipes[0]);
    pid_t child = 0;
    int started = posix_spawnp(&child, argv[0], &actions, nullptr, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipes[1]);
    if (started != 0) die("cannot run", argv[0]);
    for (ssize_t got = 1; got > 0;) { // each pass reads what the child wrote, until it closes
        got = read(pipes[0], buffer, sizeof buffer);
        if (got > 0) sha256_update(sha, buffer, (size_t)got);
    }
    close(pipes[0]);
    int status = 0;
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) die("failed:", argv[1]);
}

static void hash_glob(Sha256 *sha, const char *pattern, bool archives) {
    glob_t found = {};
    glob(pattern, 0, nullptr, &found);
    for (size_t i = 0; i < found.gl_pathc; i++) {
        if (archives) {
            hash_output(sha, (const char *[]){tool("llvm-ar"), "p", found.gl_pathv[i], nullptr});
        } else {
            hash_output(sha, (const char *[]){"cat", found.gl_pathv[i], nullptr});
        }
    }
    globfree(&found);
}

// Step 4, the identity, written last (scripts/llvm.sh says what it is for): the version and a hash of the archives
// smith links (clang's, LLVM's, lld's and clangd's, by their members) and of the objects made here.
static void write_identity(const char *stamp) {
    Sha256 sha;
    sha256_init(&sha);
    char lib[path_max];
    join(lib, "%s/install/lib", cache, "");
    hash_glob(&sha, path("%s/libclang[A-Z]*.a", lib, ""), true);
    hash_glob(&sha, path("%s/libLLVM*.a", lib, ""), true);
    hash_glob(&sha, path("%s/liblldELF.a", lib, ""), true);
    hash_glob(&sha, path("%s/liblldCommon.a", lib, ""), true);
    hash_glob(&sha, path("%s/libclangd[A-Z]*.a", lib, ""), true);
    hash_glob(&sha, path("%s/liblldb[A-Z]*.a", lib, ""), true);
    hash_glob(&sha, path("%s/libclangmain.a", lib, ""), true);
    hash_glob(&sha, path("%s/build/smith-tools/out/*.o", cache, ""), false);
    hash_glob(&sha, path("%s/build/smith-lldb/deferred/*.o", cache, ""), false);
    char hex[65];
    sha256_final(&sha, hex);
    FILE *id = fopen(path("%s/smith-clang.id", lib, ""), "w");
    if (id == nullptr || fprintf(id, "llvm-23.1.3-linux %s\n", hex) < 0 || fclose(id) != 0) die("cannot write", lib);
    FILE *out = fopen(stamp, "w");
    if (out == nullptr || fprintf(out, "%s/install\n", cache) < 0 || fclose(out) != 0) die("cannot write", stamp);
    printf("llvm_linux: llvm-23.1.3-linux %s in %s/install\n", hex, cache);
}

// Step 4, the prover (issue #820; build_prove_libs_run, build_bitwuzla_run and build_enzyme_run in build.c do the same
// on macOS): Z3, the libraries of Alive2 and Bitwuzla with what it needs, all static, into <LLVM folder>/prove/{lib,
// include}; and Enzyme, into $CACHE/smith/enzyme/0.0.303-<name>/lib. The packages are the ones build.c pins,
// with the same hashes, built by the host's compiler (the one that built LLVM), which makes position independent
// objects: smith is a PIE.

static char llvm_name[256]; // the configuration, 23.1.3-r13
static char prove_folder[path_max];
static char work_folder[path_max]; // cache/prove-work: the builds of step 5

static void run_in(const char *folder, const char *const *argv) {
    char here[path_max];
    if (getcwd(here, sizeof here) == nullptr || chdir(folder) != 0) die("cannot enter", folder);
    run(argv);
    if (chdir(here) != 0) die("cannot return to", here);
}

static bool exists(const char *file) {
    struct stat info;
    return stat(file, &info) == 0;
}

static void make_folder(const char *folder) {
    run((const char *[]){"mkdir", "-p", folder, nullptr});
}

static unsigned long cores(void) {
    long found = sysconf(_SC_NPROCESSORS_ONLN);
    return found > 0 ? (unsigned long)found : 1;
}

// A package by its hash: downloaded once into $CACHE/packages, checked, and unpacked (the folder inside the archive
// removed) into a folder of its own, which is returned.
static const char *package(const char *name, const char *url, const char *sha) {
    char archive[path_max];
    join(archive, "%s/packages", cache, "");
    make_folder(archive);
    join(archive, "%s/packages/%s", cache, sha);
    char hex[65];
    if (!exists(archive) || !sha256_file(archive, hex) || strcmp(hex, sha) != 0) {
        const char *part = path("%s.part", archive, "");
        run((const char *[]){"curl", "-fsSL", "--retry", "3", "-o", part, url, nullptr});
        if (!sha256_file(part, hex) || strcmp(hex, sha) != 0) die("the hash of the package is wrong:", url);
        if (rename(part, archive) != 0) die("cannot move", part);
    }
    const char *folder = path("%s/packages/%s.src", cache, name);
    const char *done = path("%s/.done", folder, "");
    if (!exists(done)) {
        run((const char *[]){"rm", "-rf", folder, nullptr});
        make_folder(folder);
        run((const char *[]){"tar", "-xf", archive, "-C", folder, "--strip-components=1", nullptr});
        run((const char *[]){"touch", done, nullptr});
    }
    return folder;
}

static void write_file(const char *file, const char *text, mode_t mode) {
    FILE *out = fopen(file, "w");
    if (out == nullptr || fputs(text, out) < 0 || fclose(out) != 0 || chmod(file, mode) != 0) die("cannot write", file);
}

static const char *jobs_flag(void) {
    static char flag[32];
    snprintf(flag, sizeof flag, "-j%lu", cores());
    return flag;
}

// Z3, installed into prove/, and its static initializers (about ten thousand, which the dynamic loader would run in
// every smith process) moved out of .init_array into __smith_prove, which shim/prove.cpp runs when `smith prove` does.
static void make_z3(const char *cmake, const char *python, const char *ninja) {
    if (exists(path("%s/.z3", prove_folder, ""))) return;
    const char *source = package("z3", "https://github.com/Z3Prover/z3/archive/refs/tags/z3-5.3.1.tar.gz",
                                 "c007089dd8d92298d2a7d14105b14e8dddfbb540062ce471c3111e0741edd8da");
    const char *build = path("%s/z3-build", work_folder, "");
    const char *argv[arguments_max] = {cmake,
                                       "-G",
                                       "Ninja",
                                       "-S",
                                       source,
                                       "-B",
                                       build,
                                       "-DCMAKE_BUILD_TYPE=Release",
                                       path("-DCMAKE_INSTALL_PREFIX=%s", prove_folder, ""),
                                       "-DCMAKE_INSTALL_LIBDIR=lib",
                                       path("-DCMAKE_MAKE_PROGRAM=%s", ninja, ""),
                                       "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
                                       "-DZ3_BUILD_LIBZ3_SHARED=OFF",
                                       "-DZ3_BUILD_EXECUTABLE=OFF",
                                       "-DZ3_BUILD_TEST_EXECUTABLES=OFF",
                                       "-DZ3_INCLUDE_GIT_HASH=OFF",
                                       "-DZ3_INCLUDE_GIT_DESCRIBE=OFF",
                                       "-DZ3_ENABLE_EXAMPLE_TARGETS=OFF",
                                       "-DZ3_BUILD_PYTHON_BINDINGS=OFF",
                                       "-DZ3_BUILD_JAVA_BINDINGS=OFF",
                                       "-DZ3_BUILD_DOTNET_BINDINGS=OFF",
                                       nullptr};
    size_t count = 0;
    while (argv[count] != nullptr)
        count++;
    if (python[0] != '\0') argv[count++] = path("-DPython3_EXECUTABLE=%s", python, "");
    argv[count] = nullptr;
    run(argv);
    run((const char *[]){"nice", ninja, "-C", build, "install", nullptr});
    const char *library = path("%s/lib/libz3.a", prove_folder, "");
    const char *deferred = path("%s/lib/libz3.deferred.a", prove_folder, "");
    defer(library, deferred, "prove");
    if (rename(deferred, library) != 0) die("cannot move", deferred);
    run((const char *[]){"touch", path("%s/.z3", prove_folder, ""), nullptr});
    run((const char *[]){"rm", "-rf", build, nullptr}); // the build is 3 GB of objects and the pack holds none
}

// Alive2's libraries that hold LLVM IR's semantics (not its translation-validation plugin or its programs), against the
// LLVM install and the Z3 just made, with the headers its own headers include, by the paths they use.
static void make_alive2(const char *cmake, const char *ninja) {
    if (exists(path("%s/.alive2", prove_folder, ""))) return;
    const char *source = package(
        "alive2", "https://github.com/AliveToolkit/alive2/archive/01a5ec45c8152995755f7331827407a9de19f262.tar.gz",
        "ea6d0ad5869a4c863ca537c65fe22e2f1198a5a1b2fc51d3a96b8020cf78cc13");
    const char *build = path("%s/alive2-build", work_folder, "");
    // Alive2 finds its version with git, which a package has none of: a stand-in that names the pin. re2c only builds
    // its own `alive` tool, which is not built here.
    const char *git = path("%s/git", work_folder, "");
    write_file(git, "#!/bin/sh\necho smith-pinned\n", 0755);
    run((const char *[]){cmake, "-G", "Ninja", "-S", source, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
                         path("-DCMAKE_MAKE_PROGRAM=%s", ninja, ""), "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
                         "-DCMAKE_CXX_FLAGS=-Wno-error -Dsmt=alive2_smt", path("-DGIT_EXECUTABLE=%s", git, ""),
                         "-DRE2C=/usr/bin/true", "-DBUILD_LLVM_UTILS=1",
                         path("-DLLVM_DIR=%s/install/lib/cmake/llvm", cache, ""),
                         path("-DZ3_INCLUDE_DIR=%s/include", prove_folder, ""),
                         path("-DZ3_LIBRARIES=%s/lib/libz3.a", prove_folder, ""), nullptr});
    run((const char *[]){"nice", ninja, "-C", build, "ir", "smt", "tools", "util", "llvm_util", nullptr});
    static const char *const folders[] = {"ir", "smt", "tools", "util", "llvm_util"};
    const char *headers = path("%s/include/alive2", prove_folder, "");
    run((const char *[]){"rm", "-rf", headers, nullptr});
    make_folder(headers);
    for (size_t i = 0; i < sizeof folders / sizeof folders[0]; i++) {
        run((const char *[]){"cp", path("%s/lib%s.a", build, folders[i]),
                             path("%s/lib/libalive2_%s.a", prove_folder, folders[i]), nullptr});
        const char *target = path("%s/%s", headers, folders[i]);
        make_folder(target);
        const char *argv[arguments_max] = {"cp"};
        size_t count = 1;
        glob_t found = {};
        glob(path("%s/%s/*.h", source, folders[i]), 0, nullptr, &found);
        glob(path("%s/%s/*.inc", source, folders[i]), GLOB_APPEND, nullptr, &found);
        for (size_t j = 0; j < found.gl_pathc && count + 3 < arguments_max; j++)
            argv[count++] = found.gl_pathv[j];
        argv[count++] = path("%s/", target, "");
        argv[count] = nullptr;
        if (found.gl_pathc > 0) run(argv);
        globfree(&found);
    }
    run((const char *[]){"cp", path("%s/version_gen.h", build, ""), path("%s/", headers, ""), nullptr});
    run((const char *[]){"touch", path("%s/.alive2", prove_folder, ""), nullptr});
    run((const char *[]){"rm", "-rf", build, nullptr});
}

// `./configure && make && make install` in a copy of a package, into prove/.
static void autotools(const char *name, const char *source, const char *const *flags) {
    const char *folder = path("%s/%s-build", work_folder, name);
    run((const char *[]){"rm", "-rf", folder, nullptr});
    run((const char *[]){"cp", "-Rp", source, folder, nullptr});
    run((const char *[]){"chmod", "-R", "u+w", folder, nullptr});
    const char *argv[arguments_max] = {"./configure", path("--prefix=%s", prove_folder, "")};
    size_t count = 2;
    for (size_t i = 0; flags[i] != nullptr; i++)
        argv[count++] = flags[i];
    argv[count] = nullptr;
    run_in(folder, argv);
    run_in(folder, (const char *[]){"make", jobs_flag(), nullptr});
    run_in(folder, (const char *[]){"make", "install", nullptr});
}

// Meson makes its static libraries as thin archives (members named by path, not held), which stop being archives once
// they are copied: this one is rewritten in place holding its members.
static void flatten(const char *archive) {
    char magic[8] = {};
    FILE *check = fopen(archive, "rb");
    if (check == nullptr || fread(magic, 1, sizeof magic, check) != sizeof magic) die("cannot read", archive);
    fclose(check);
    if (memcmp(magic, "!<thin>\n", sizeof magic) != 0) return;
    char folder[path_max];
    snprintf(folder, sizeof folder, "%s", archive);
    *strrchr(folder, '/') = '\0';
    const char *list = path("%s.list", archive, "");
    int output = open(list, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output < 0 || !run_to((const char *[]){tool("llvm-ar"), "t", archive, nullptr}, output))
        die("cannot list", archive);
    close(output);
    static char names[arguments_max][path_max];
    const char *argv[arguments_max] = {tool("llvm-ar"), "rcs", "flat.a"};
    size_t count = 3;
    FILE *members = fopen(list, "r");
    if (members == nullptr) die("cannot read", list);
    while (count + 1 < arguments_max && fgets(names[count], sizeof names[count], members) != nullptr) {
        names[count][strcspn(names[count], "\n")] = '\0';
        argv[count] = names[count];
        count++;
    }
    fclose(members);
    argv[count] = nullptr;
    run_in(folder, argv);
    if (rename(path("%s/flat.a", folder, ""), archive) != 0) die("cannot move", archive);
}

// Bitwuzla (the solver `smith prove` asks first; Z3 only finds the model of a counterexample) and what it needs: GMP
// and MPFR with their own configure; CaDiCaL and symfpu, which Meson would fetch, placed where its wraps' downloads
// would go with the wraps' overlays; Bitwuzla's Meson told where the static GMP and MPFR are instead of asking
// pkg-config.
static void make_bitwuzla(const char *python, const char *ninja) {
    if (exists(path("%s/.bitwuzla", prove_folder, ""))) return;
    const char *bitwuzla = package("bitwuzla", "https://github.com/bitwuzla/bitwuzla/archive/refs/tags/0.9.1.tar.gz",
                                   "42707f38900a20bb18108e426ba667560d1fd2ccce0d4f75aa60439b546488b4");
    const char *meson =
        package("meson", "https://github.com/mesonbuild/meson/releases/download/1.9.1/meson-1.9.1.tar.gz",
                "4e076606f2afff7881d195574bddcd8d89286f35a17b4977a216f535dc0c74ac");
    const char *gmp = package("gmp", "https://ftp.gnu.org/gnu/gmp/gmp-6.3.0.tar.xz",
                              "a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898");
    const char *mpfr = package("mpfr", "https://ftp.gnu.org/gnu/mpfr/mpfr-4.2.2.tar.xz",
                               "b67ba0383ef7e8a8563734e2e889ef5ec3c3b898a01d00fa0a6869ad81c6ce01");
    const char *cadical = package("cadical", "https://github.com/arminbiere/cadical/archive/rel-2.1.2.tar.gz",
                                  "292c2bb8d712d6d05fce3d3df63b922b8fa45e03974a79f7bae5bf68c284f131");
    const char *symfpu =
        package("symfpu", "https://github.com/martin-cs/symfpu/archive/40bdec00e99f8ea1b96c3dac0a05eed11c541639.tar.gz",
                "ba17877fbf0c851e113fddaab225152f1c0b2044429396b56b2f113832e36ce5");
    if (!exists(path("%s/lib/libmpfr.a", prove_folder, ""))) {
        autotools("gmp", gmp,
                  (const char *[]){"--disable-shared", "--enable-static", "--enable-cxx", "--with-pic", nullptr});
        autotools("mpfr", mpfr,
                  (const char *[]){path("--with-gmp=%s", prove_folder, ""), "--disable-shared", "--enable-static",
                                   "--with-pic", nullptr});
    }
    // Meson writes its subprojects into the source tree: build from a copy, with the two subprojects in place.
    const char *source = path("%s/bitwuzla-src", work_folder, "");
    const char *build = path("%s/bitwuzla-build", work_folder, "");
    const char *subprojects = path("%s/subprojects", source, "");
    const char *cadical_folder = path("%s/cadical-rel-2.1.2", subprojects, "");
    const char *symfpu_folder = path("%s/symfpu", subprojects, "");
    run((const char *[]){"rm", "-rf", source, build, nullptr});
    run((const char *[]){"cp", "-R", bitwuzla, source, nullptr});
    run((const char *[]){"chmod", "-R", "u+w", source, nullptr});
    run((const char *[]){"cp", "-R", cadical, cadical_folder, nullptr});
    run((const char *[]){"cp", "-R", path("%s/packagefiles/cadical/.", subprojects, ""),
                         path("%s/", cadical_folder, ""), nullptr});
    run((const char *[]){"cp", "-R", symfpu, symfpu_folder, nullptr});
    run((const char *[]){"cp", "-R", path("%s/packagefiles/symfpu/.", subprojects, ""), path("%s/", symfpu_folder, ""),
                         nullptr});
    run((const char *[]){"chmod", "-R", "u+w", subprojects, nullptr});
    // GMP and MPFR by find_library, in the one place Bitwuzla looks them up.
    char libdir[path_max];
    join(libdir, "%s/lib", prove_folder, "");
    setenv("LIBDIR", libdir, 1);
    run((const char *[]){
        "perl", "-0pi", "-e",
        "s|gmp_dep = dependency\\(.gmp.,.*?static: build_static\\)\\nmpfr_dep = dependency\\(.mpfr.,.*?static: "
        "build_static\\)|gmp_dep = cpp_compiler.find_library(\\x27gmp\\x27, dirs: [\\x27$ENV{LIBDIR}\\x27], static: "
        "true)\\nmpfr_dep = cpp_compiler.find_library(\\x27mpfr\\x27, dirs: [\\x27$ENV{LIBDIR}\\x27], static: true)|s",
        path("%s/src/meson.build", source, ""), nullptr});
    if (!run_to((const char *[]){"grep", "-q", "find_library('gmp'", path("%s/src/meson.build", source, ""), nullptr},
                -1))
        die("Bitwuzla's src/meson.build changed in a way this function does not know", nullptr);
    run((const char *[]){python[0] != '\0' ? python : "python3", "-I", path("%s/meson.py", meson, ""), "setup", build,
                         source, "--buildtype=release", "--default-library=static",
                         path("--prefix=%s", prove_folder, ""), "--wrap-mode=nodownload",
                         path("-Dc_args=-I%s/include", prove_folder, ""),
                         path("-Dcpp_args=-I%s/include", prove_folder, ""), "-Dtesting=disabled",
                         "-Dunit_testing=disabled", "-Dpython=false", "-Ddocs=false", nullptr});
    static const char *const libraries[] = {"subprojects/cadical-rel-2.1.2/src/libcadical.a",
                                            "src/libbzlautil.a",
                                            "src/lib/libbzlarng.a",
                                            "src/lib/libbitwuzlabv.a",
                                            "src/lib/libbitwuzlabb.a",
                                            "src/lib/libbitwuzlals.a",
                                            "src/libbitwuzla.a"};
    const char *argv[arguments_max] = {"nice", ninja, "-C", build};
    size_t count = 4;
    for (size_t i = 0; i < sizeof libraries / sizeof libraries[0]; i++)
        argv[count++] = libraries[i];
    argv[count] = nullptr;
    run(argv);
    for (size_t i = 0; i < sizeof libraries / sizeof libraries[0]; i++) {
        flatten(path("%s/%s", build, libraries[i]));
        run((const char *[]){"cp", path("%s/%s", build, libraries[i]), path("%s/lib/", prove_folder, ""), nullptr});
    }
    const char *headers = path("%s/include/bitwuzla", prove_folder, "");
    make_folder(headers);
    run((const char *[]){"cp", "-R", path("%s/include/bitwuzla/.", source, ""), path("%s/", headers, ""), nullptr});
    run((const char *[]){"chmod", "-R", "u+w", headers, nullptr});
    run((const char *[]){"touch", path("%s/.bitwuzla", prove_folder, ""), nullptr});
    run((const char *[]){"rm", "-rf", source, build, path("%s/gmp-build", work_folder, ""),
                         path("%s/mpfr-build", work_folder, ""), nullptr});
}

static void make_prover(const char *cmake, const char *python, const char *ninja) {
    join(prove_folder, "%s/prove", cache, "");
    join(work_folder, "%s/prove-work", cache, "");
    make_folder(path("%s/lib", prove_folder, ""));
    make_folder(path("%s/include", prove_folder, ""));
    make_folder(work_folder);
    // Meson and CMake find ninja on PATH, and the pinned one may not be there.
    if (strchr(ninja, '/') != nullptr) {
        char folder[path_max];
        snprintf(folder, sizeof folder, "%s", ninja);
        *strrchr(folder, '/') = '\0';
        const char *old = getenv("PATH");
        char joined[path_max * 2];
        snprintf(joined, sizeof joined, "%s:%s", folder, old != nullptr ? old : "");
        setenv("PATH", joined, 1);
    }
    make_z3(cmake, python, ninja);
    make_alive2(cmake, ninja);
    make_bitwuzla(python, ninja);
}

// Enzyme v0.0.303 (reverse and forward mode automatic differentiation of LLVM IR), static, against the LLVM install,
// with patches/enzyme/*.patch applied to a copy: build_enzyme_make in build.c says why each option.
static void make_enzyme(const char *cmake, const char *ninja) {
    const char *out = path("%s/../../enzyme/0.0.303-%s", cache, llvm_name);
    const char *library = path("%s/lib/libEnzymeStatic-23.a", out, "");
    if (exists(library)) return;
    const char *source = package("enzyme", "https://github.com/EnzymeAD/Enzyme/archive/refs/tags/v0.0.303.tar.gz",
                                 "0ca3114d008e524cd3b13152bbc172794b20af643d1628161655373eba4554de");
    const char *work = path("%s/enzyme-work", cache, "");
    const char *copy = path("%s/src", work, "");
    run((const char *[]){"rm", "-rf", work, nullptr});
    make_folder(copy);
    make_folder(path("%s/lib", out, ""));
    run((const char *[]){"cp", "-R", path("%s/.", source, ""), path("%s/", copy, ""), nullptr});
    run((const char *[]){"chmod", "-R", "u+w", copy, nullptr});
    char here[path_max]; // patch -d changes folder before it opens the patch: its path is absolute
    if (getcwd(here, sizeof here) == nullptr) die("cannot tell the current folder", nullptr);
    glob_t patches = {};
    glob("patches/enzyme/*.patch", 0, nullptr, &patches);
    for (size_t i = 0; i < patches.gl_pathc; i++)
        run((const char *[]){"patch", "-p1", "-s", "-d", copy, "-i", path("%s/%s", here, patches.gl_pathv[i]),
                             nullptr});
    globfree(&patches);
    const char *build = path("%s/build", work, "");
    run((const char *[]){
        cmake, "-G", "Ninja", "-S", path("%s/enzyme", copy, ""), "-B", build, "-DCMAKE_BUILD_TYPE=Release",
        path("-DCMAKE_MAKE_PROGRAM=%s", ninja, ""), "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
        path("-DLLVM_DIR=%s/install/lib/cmake/llvm", cache, ""),
        path("-DClang_DIR=%s/install/lib/cmake/clang", cache, ""), "-DENZYME_STATIC_LIB=ON", "-DENZYME_CLANG=OFF",
        "-DLLVM_EXTERNAL_LIT=/nonexistent", "-DCMAKE_CXX_FLAGS=-DENZYME_RUNPASS", nullptr});
    run((const char *[]){"nice", ninja, "-C", build, "EnzymeStatic-23", nullptr});
    run((const char *[]){"cp", path("%s/Enzyme/libEnzymeStatic-23.a", build, ""), library, nullptr});
    char id[path_max];
    snprintf(id, sizeof id, "enzyme-0.0.303 llvm-%s\n", llvm_name);
    write_file(path("%s/lib/smith-enzyme.id", out, ""), id, 0644);
    run((const char *[]){"rm", "-rf", work, nullptr});
}

static void find_cache(void) {
    const char *xdg = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");
    const char *name = getenv("SMITH_LLVM_NAME");
    if (name == nullptr || *name == '\0') name = "23.1.3-r13";
    snprintf(llvm_name, sizeof llvm_name, "%s", name);
    if (xdg != nullptr && *xdg == '/') {
        snprintf(cache, sizeof cache, "%s/smith/llvm/%s", xdg, name);
    } else if (home != nullptr && *home == '/') {
        snprintf(cache, sizeof cache, "%s/.cache/smith/llvm/%s", home, name);
    } else {
        die("no cache folder: set XDG_CACHE_HOME or HOME", nullptr);
    }
    snprintf(tools, sizeof tools, "%s/install/bin", cache);
}

int main(int argc, char **argv) {
    if (argc != 6) die("usage: llvm_linux <llvm source> <cmake> <python> <stamp> <ninja>", nullptr);
    const char *cmake = argv[2][0] != '\0' ? argv[2] : "cmake";
    const char *python = argv[3][0] != '\0' ? argv[3] : "python3";
    const char *ninja = argv[5][0] != '\0' ? argv[5] : "ninja";
    find_cache();
    char stamp_folder[path_max];
    snprintf(stamp_folder, sizeof stamp_folder, "%s", argv[4]);
    char *slash = strrchr(stamp_folder, '/');
    if (slash != nullptr) *slash = '\0';
    run((const char *[]){"mkdir", "-p", cache, slash != nullptr ? stamp_folder : ".", nullptr});
    // LLVM_LINUX_ONLY=prover or enzyme builds only that step, on an LLVM that is there (to try one without the hours of
    // the rest), and writes no identity.
    const char *only = getenv("LLVM_LINUX_ONLY");
    bool all = only == nullptr || *only == '\0';
    if (all) {
        apply_patches(argv[1]);
        configure_and_build(cmake, python, ninja);
        make_clang_main();
        make_tools();
        make_lldb();
    }
    if (all || strcmp(only, "prover") == 0) make_prover(cmake, python, ninja);
    if (all || strcmp(only, "enzyme") == 0) make_enzyme(cmake, ninja);
    if (all) write_identity(argv[4]);
    return 0;
}
