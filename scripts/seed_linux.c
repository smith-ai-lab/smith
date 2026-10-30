// Builds the first smith on a Linux machine from source, with no smith and no release: the seed of issue #305's Linux
// release chain, and its way back when the chain breaks (knowledge/smith-bootstrap.md). Everyday Linux clones get a
// release from scripts/stage0.c instead, which runs this only when no release has a Linux build. This file has no
// `module` line, so it is plain C23 (with POSIX's setenv, hence gnu2x, which GCC 13 and clang both read) and the
// system's C compiler builds it (clang 19 or later, GCC 13 or later):
//   mkdir -p .smith && cc -std=gnu2x -x c scripts/seed_linux.c -o .smith/seed-tool && .smith/seed-tool [arguments
//   of smith build]
// It needs a C and C++ compiler, lld (the link of smith: `-fuse-ld=lld`), tar, xz, gh and the network; with a published
// LLVM pack that is minutes. With no pack it also needs cmake, ninja, python3, curl, patch and git, and about 2 hours
// and 15 GB the first time (LLVM). The stages, each in .smith/seed/:
//   1. LLVM 23.1.3 in the package cache (as `smith build llvm` has it): the published pack of this configuration
//      (`llvm-23.1.3-r11-linux-x86_64`, fetched and checked by scripts/llvm_pack.c, which needs `gh`) unless that
//      install is there already; only when no pack exists, built from the source pinned by SHA-256 by
//      scripts/llvm_linux.c;
//   2. the libraries' headers, which are build outputs and not in git: scripts/header_tool.c, the writer of
//      `smith cc --smith-header` as a program of its own, built with this compiler and run on lib/smith/*.smith;
//   3. stage A: smith.c without `smith mcp` (no -DSMITH_MCP: the protocol code does not exist yet), its shims and that
//      LLVM, linked with weak stand-ins for the six libraries, which are .smith and need a smith to compile. Stage A
//      has clang and the token hook, so it compiles .smith; any call into a stand-in stops it with a message;
//   4. the six libraries, compiled by stage A, and the protocol code (DAP's and MCP's, which are build outputs and not
//      in git): scripts/protocol.c, built with stage A's smith-cc and the libraries, run on schemas/;
//   5. stage B: smith.c again, now with `smith mcp` and the protocol code, linked with the libraries, a whole smith,
//      which then runs `smith build` with the arguments given, as stage0's release would.
#include <errno.h>
#include <glob.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "sha256.h"

extern char **environ;

constexpr size_t path_max = 4096;      // PATH_MAX of Linux, which strict C23 does not define
constexpr size_t arguments_max = 2048; // one command: a link names every archive of the LLVM install
constexpr size_t text_max = 1 << 20;   // a header's first lines, or the identity

static const char llvm_url[] =
    "https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.3/llvm-project-23.1.3.src.tar.xz";
static const char llvm_sha256[] = "c44186a7762ed28954be72e5ff6df9808e0779d4f1bf014ecc4e7e211d31ee34";
static const char *const libraries[] = {"arena", "contract", "inflate", "io", "random", "text"}; // build.c's list
// The modules whose headers smith.c includes: the libraries it links, and smith::build's, which build_runtime.c reads.
static const char *const header_modules[] = {"arena", "contract", "inflate", "io", "random", "text", "build"};
static const char *const shims[] = {"clang",  "lld",           "format",      "tidy",     "clangd",       "symbolize",
                                    "hook",   "lsp",           "lldb",        "lldb_dap", "lldb_explain", "lldb_slices",
                                    "affine", "effects_infer", "lldb_server", "coverage"};
static const char *const cflags[] = {"-std=gnu23", "-O1", "-g", "-Wall", "-Wextra", "-Wpedantic"};

static char cwd[path_max];      // the repository's root, where this runs
static char llvm[path_max];     // the LLVM install: <cache>/smith/llvm/<name>/install
static char identity[text_max]; // its smith-clang.id, one line

[[noreturn]] static void die(const char *message, const char *detail) {
    fprintf(stderr, "seed: %s%s%s\n", message, detail != nullptr ? " " : "", detail != nullptr ? detail : "");
    exit(1);
}

// Runs a program (found on PATH) and waits; its exit status, or -1 when it did not start or did not exit.
static int run_status(const char *const *argv) {
    pid_t child = 0;
    if (posix_spawnp(&child, argv[0], nullptr, nullptr, (char *const *)argv, environ) != 0) return -1;
    int status = 0;
    while (waitpid(child, &status, 0) < 0) { // only a signal interrupts the wait
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void run(const char *const *argv) {
    if (run_status(argv) != 0) die("failed:", argv[0]);
}

static void join(char *out, const char *format, const char *a, const char *b) {
    int written = snprintf(out, path_max, format, a, b);
    if (written < 0 || (size_t)written >= path_max) die("a path is too long:", a);
}

// A path for one call: it lives until 16 more are made.
static const char *path(const char *format, const char *a, const char *b) {
    static char paths[16][path_max];
    static size_t next = 0;
    char *out = paths[next++ % 16];
    join(out, format, a, b);
    return out;
}

static bool exists(const char *file) {
    struct stat info;
    return stat(file, &info) == 0;
}

// A list of arguments built up for one command.
typedef struct {
    const char *items[arguments_max];
    size_t count;
} Args;

static void add(Args *args, const char *item) {
    if (args->count + 2 > arguments_max) die("a command is too long at", item);
    args->items[args->count++] = item;
    args->items[args->count] = nullptr;
}

static void add_all(Args *args, const char *const *items, size_t count) {
    for (size_t i = 0; i < count; i++)
        add(args, items[i]);
}

// Copies of glob's results that outlive the glob.
static void add_glob(Args *args, const char *pattern) {
    static char kept[arguments_max][path_max];
    static size_t used = 0;
    glob_t found = {};
    glob(pattern, 0, nullptr, &found);
    for (size_t i = 0; i < found.gl_pathc; i++) {
        if (used == arguments_max) die("too many files for", pattern);
        snprintf(kept[used], path_max, "%s", found.gl_pathv[i]);
        add(args, kept[used++]);
    }
    globfree(&found);
}

static bool read_text(const char *file, char *into, size_t capacity) {
    FILE *in = fopen(file, "rb");
    if (in == nullptr) return false;
    size_t got = fread(into, 1, capacity - 1, in);
    into[got] = '\0';
    fclose(in);
    return true;
}

// Stage 2: the headers of the .smith modules smith.c includes, written by scripts/header_tool.c (a plain C program, no
// smith needed) into .smith/seed/include/smith/, where stage A's compiles and stand-ins read them.
static void write_headers(const char *cc) {
    run((const char *[]){"mkdir", "-p", ".smith/seed/include/smith", nullptr});
    // -lm: glibc keeps floor and the rest of <math.h> in libm, which the lowering in header_tool calls
    run((const char *[]){cc, "-std=gnu2x", "-O1", "-x", "c", "scripts/header_tool.c", "-o", ".smith/seed/header_tool",
                         "-lm", nullptr});
    for (size_t i = 0; i < sizeof header_modules / sizeof header_modules[0]; i++) {
        run((const char *[]){".smith/seed/header_tool", path("lib/smith/%s.smith", header_modules[i], ""), "-o",
                             path(".smith/seed/include/smith/%s.h", header_modules[i], ""), nullptr});
    }
}

// Takes the LLVM pack of this configuration into the cache: true when it did, false when no pack exists (build
// instead).
static bool fetch_pack(const char *cc, const char *smith_cache, const char *name) {
    if (getenv("SMITH_SEED_FETCH") != nullptr && strcmp(getenv("SMITH_SEED_FETCH"), "0") == 0) return false;
    run((const char *[]){cc, "-std=gnu2x", "-O1", "-x", "c", "scripts/llvm_pack.c", "-o", ".smith/seed/llvm_pack",
                         nullptr});
    printf("seed: fetching the LLVM pack %s\n", name);
    fflush(stdout);
    int status = run_status(
        (const char *[]){".smith/seed/llvm_pack", "fetch", smith_cache, name, ".smith/seed/llvm.fetch", nullptr});
    if (status == 0) return true;
    if (status != 3) die("the LLVM pack failed to fetch (exit status not 3, so the pack exists):", name);
    printf("seed: no LLVM pack for %s: building LLVM from source\n", name);
    fflush(stdout);
    return false;
}

// Builds the LLVM install from the pinned source (about 2 hours): scripts/llvm_linux.c.
static void build_llvm_source(const char *cc) {
    const char *tarball = ".smith/seed/llvm-project-23.1.3.src.tar.xz";
    char digest[65] = "";
    if (!sha256_file(tarball, digest) || strcmp(digest, llvm_sha256) != 0) {
        printf("seed: downloading %s\n", llvm_url);
        fflush(stdout);
        run((const char *[]){"curl", "-fsSL", "-o", tarball, llvm_url, nullptr});
        if (!sha256_file(tarball, digest) || strcmp(digest, llvm_sha256) != 0) die("wrong SHA-256:", tarball);
    }
    run((const char *[]){"rm", "-rf", ".smith/seed/llvm-src", nullptr});
    run((const char *[]){"mkdir", "-p", ".smith/seed/llvm-src", nullptr});
    run((const char *[]){"tar", "-xJf", tarball, "-C", ".smith/seed/llvm-src", "--strip-components=1", nullptr});
    run((const char *[]){cc, "-std=gnu2x", "-O1", "-x", "c", "scripts/llvm_linux.c", "-o", ".smith/seed/llvm_linux",
                         nullptr});
    run((const char *[]){".smith/seed/llvm_linux", ".smith/seed/llvm-src", "", "", ".smith/seed/llvm.stamp", "",
                         nullptr});
}

// Stage 1: the LLVM install: the published pack (scripts/llvm_pack.c fetch: downloaded with gh, each part checked
// against the SHA-256 of the release's manifest) unless it is there; else built by scripts/llvm_linux.c from the
// pinned source. `fetch` exits 3 when there is no pack to take (no release, no gh, or a build in that folder): only
// that sends the seed to the long build; any other failure of a pack that exists stops it.
static void build_llvm(const char *cc) {
    const char *cache = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");
    const char *name = getenv("SMITH_LLVM_NAME");
    char smith_cache[path_max]; // the package cache's smith folder
    if (name == nullptr || *name == '\0') name = "23.1.3-r11";
    if (cache != nullptr && *cache == '/') {
        join(smith_cache, "%s/smith", cache, "");
    } else if (home != nullptr && *home == '/') {
        join(smith_cache, "%s/.cache/smith", home, "");
    } else {
        die("no cache folder: set XDG_CACHE_HOME or HOME", nullptr);
    }
    join(llvm, "%s/llvm/%s/install", smith_cache, name);
    if (!exists(path("%s/lib/smith-clang.id", llvm, "")) && !fetch_pack(cc, smith_cache, name)) build_llvm_source(cc);
    if (!read_text(path("%s/lib/smith-clang.id", llvm, ""), identity, sizeof identity)) die("no identity in", llvm);
    identity[strcspn(identity, "\n")] = '\0';
}

// Compiles one source into .smith/seed/<stage>/<name>.o with `compiler` and the flags given.
static const char *compile(const char *compiler, const char *source, const char *stage, const char *name,
                           const Args *flags) {
    static char outputs[64][path_max];
    static size_t used = 0;
    if (used == 64) die("too many objects", nullptr);
    char *output = outputs[used++];
    join(output, ".smith/seed/%s/%s.o", stage, name);
    Args args = {};
    add(&args, compiler);
    add_all(&args, flags->items, flags->count);
    add(&args, "-c");
    add(&args, source);
    add(&args, "-o");
    add(&args, output);
    run(args.items);
    return output;
}

// smith.c's flags, as build.c gives them (and the libraries the same, so that `smith prove` finds their bodies).
static void smith_flags(Args *flags, bool mcp) {
    static char id_flag[text_max + 32];
    snprintf(id_flag, sizeof id_flag, "-DSMITH_CLANG_ID=\"%s\"", identity);
    add_all(flags, cflags, sizeof cflags / sizeof cflags[0]);
    static const char *const more[] = {
        "-I",           "include",     "-I", ".smith/seed/include", "-DSMITH_COMMIT_LINKED", "-DSMITH_IN_PROCESS",
        "-DSMITH_LLDB", "-DSMITH_HOOK"};
    add_all(flags, more, sizeof more / sizeof more[0]);
    if (mcp) { // stage B: the protocol code is in .smith/seed/protocol by then
        static const char *const protocol[] = {"-DSMITH_MCP", "-I", ".smith/seed/protocol", "-I", "runtime/protocol"};
        add_all(flags, protocol, sizeof protocol / sizeof protocol[0]);
    }
    add(flags, id_flag);
}

// The objects every stage links: the packed resource directory, the commit, smith.c and the shims.
static void compile_smith(Args *objects) {
    const char *clang = path("%s/bin/clang", llvm, "");
    Args base = {};
    add_all(&base, cflags, sizeof cflags / sizeof cflags[0]);
    run((const char *[]){clang, "-std=gnu23", "-O1", "scripts/pack.c", "-o", ".smith/seed/a/pack", nullptr});
    char script[path_max];
    join(script,
         "cd \"$0\" && (find lib/clang/*/include lib/clang/*/share -type f; ls lib/clang/*/lib/*-linux-gnu/*) "
         "| LC_ALL=C sort | \"$1\" \"$2\"",
         "", "");
    char pack_tool[path_max];
    char pack[path_max];
    join(pack_tool, "%s/.smith/seed/a/pack", cwd, "");
    join(pack, "%s/.smith/seed/a/clang-resource.pack", cwd, "");
    run((const char *[]){"sh", "-c", script, llvm, pack_tool, pack, nullptr});
    Args data = base;
    static char resource[path_max];
    join(resource, "-DSMITH_CLANG_RESOURCE=\"%s\"", pack, "");
    add(&data, resource);
    add(objects, compile(clang, "runtime/pack_data.c", "a", "pack_data", &data));
    add(objects, compile(clang, "runtime/commit.c", "a", "commit", &base));
    static const char *const cxx[] = {"-std=c++17",
                                      "-O1",
                                      "-g",
                                      "-fno-exceptions",
                                      "-fno-rtti",
                                      "-D__STDC_CONSTANT_MACROS",
                                      "-D__STDC_FORMAT_MACROS",
                                      "-D__STDC_LIMIT_MACROS",
                                      "-DSMITH_HOOK",
                                      "-isystem",
                                      "shim/lldb",
                                      "-isystem"};
    // The shims are compiled by the system's C++ compiler, the one that built the pack's libraries (GCC on the hosted
    // image): GCC and clang mangle `std::enable_if<std::is_integral_v<bool>, ...>` differently, so clang-built shims
    // would not find clang-tidy's `OptionsView::get<bool>` in libclangTidy.a. Everything else is clang's.
    const char *cxx_compiler = getenv("CXX") != nullptr && *getenv("CXX") != '\0' ? getenv("CXX") : "c++";
    Args shim_flags = {};
    add_all(&shim_flags, cxx, sizeof cxx / sizeof cxx[0]);
    static char include[path_max];
    join(include, "%s/include", llvm, "");
    add(&shim_flags, include);
    for (size_t i = 0; i < sizeof shims / sizeof shims[0]; i++) {
        add(objects,
            compile(cxx_compiler, path("shim/%s.cpp", shims[i], ""), "a", path("%s_shim", shims[i], ""), &shim_flags));
    }
}

// The generated protocol code: scripts/protocol.c with stage A's smith-cc for the modules it stands on (arena,
// contract, text), and the plain C of lib/json.c and lib/trace.c, run on the vendored schemas into
// .smith/seed/protocol/.
static void write_protocol(void) {
    const char *clang = path("%s/bin/clang", llvm, "");
    run((const char *[]){"mkdir", "-p", ".smith/seed/protocol", ".smith/seed/p", nullptr});
    Args flags = {};
    smith_flags(&flags, false);
    Args objects = {};
    static const char *const c_sources[][2] = {
        {"scripts/protocol.c", "protocol"}, {"lib/json.c", "json"}, {"lib/trace.c", "trace"}};
    for (size_t i = 0; i < sizeof c_sources / sizeof c_sources[0]; i++)
        add(&objects, compile(clang, c_sources[i][0], "p", c_sources[i][1], &flags));
    static const char *const modules[] = {"arena", "contract", "text"};
    for (size_t i = 0; i < sizeof modules / sizeof modules[0]; i++)
        add(&objects,
            compile(".smith/seed/a/bin/smith-cc", path("lib/smith/%s.smith", modules[i], ""), "p", modules[i], &flags));
    Args link = {};
    add(&link, clang);
    add(&link, "-o");
    add(&link, ".smith/seed/p/protocol_tool");
    add_all(&link, objects.items, objects.count);
    add(&link, "-lm");
    run(link.items);
    static const char *const schemas[][3] = {
        {"dap", "schemas/dap/debugAdapterProtocol.json",
         "debugAdapterProtocol.json of microsoft/debug-adapter-protocol v1.71.0"},
        {"mcp", "schemas/mcp/schema.json",
         "schema/2026-07-28/schema.json of modelcontextprotocol/modelcontextprotocol, release 2026-07-28"}};
    for (size_t i = 0; i < sizeof schemas / sizeof schemas[0]; i++) {
        run((const char *[]){".smith/seed/p/protocol_tool", schemas[i][0], schemas[i][1],
                             path("schemas/%s/LICENSE", schemas[i][0], ""), schemas[i][2], ".smith/seed/protocol",
                             ".smith/seed/protocol", nullptr});
    }
}

// Stand-ins for the libraries' functions, for stage A: each name a library's header says has a body (its
// `smith-bodies` line, by the C name its `smith-names` line gives it), a weak function that stops with a message.
static void write_stand_ins(FILE *out, const char *library) {
    static char header[text_max];
    if (!read_text(path(".smith/seed/include/smith/%s.h", library, ""), header, sizeof header))
        die("cannot read the header of", library);
    const char *names = strstr(header, "// smith-names: ");
    const char *bodies = strstr(header, "// smith-bodies: ");
    if (names == nullptr || bodies == nullptr) return;
    names += strlen("// smith-names: ");
    bodies += strlen("// smith-bodies: ");
    size_t names_length = strcspn(names, "\n");
    for (const char *body = bodies; *body != '\n' && *body != '\0';) { // each pass moves past one name
        size_t length = strcspn(body, " \n");
        for (const char *at = names; at < names + names_length;) { // each pass moves past one name=c_name
            size_t pair = strcspn(at, " \n");
            const char *equals = memchr(at, '=', pair);
            if (equals != nullptr && (size_t)(equals - at) == length && memcmp(at, body, length) == 0) {
                int c_length = (int)(pair - (size_t)(equals + 1 - at));
                fprintf(out, "[[gnu::weak]] void %.*s(void) { seed_stop(\"%.*s\"); }\n", c_length, equals + 1, c_length,
                        equals + 1);
            }
            at += pair;
            if (*at != ' ') break;
            at++;
        }
        body += length + (body[length] == ' ' ? 1 : 0);
    }
}

static const char *compile_stand_ins(void) {
    FILE *out = fopen(".smith/seed/a/stand_ins.c", "w");
    if (out == nullptr) die("cannot write", ".smith/seed/a/stand_ins.c");
    fputs("#include <stdio.h>\n#include <stdlib.h>\n[[noreturn]] static void seed_stop(const char *name) {\n"
          "    fprintf(stderr, \"smith (seed stage A): %s is in a library this smith does not have yet\\n\", name);\n"
          "    abort();\n}\n",
          out);
    for (size_t i = 0; i < sizeof libraries / sizeof libraries[0]; i++)
        write_stand_ins(out, libraries[i]);
    if (fclose(out) != 0) die("cannot write", ".smith/seed/a/stand_ins.c");
    Args base = {};
    add_all(&base, cflags, sizeof cflags / sizeof cflags[0]);
    return compile(path("%s/bin/clang", llvm, ""), ".smith/seed/a/stand_ins.c", "a", "stand_ins", &base);
}

// Links smith from the objects given and the LLVM install, as build.c does on Linux (its BuildPlatform for Linux).
static void link_smith(const Args *objects, const char *output) {
    static char clang[path_max];
    static char tools[path_max];
    join(clang, "%s/bin/clang", llvm, "");
    join(tools, "%s/lib/libsmithtools.a", llvm, "");
    Args args = {};
    add(&args, clang);
    add(&args, "-fuse-ld=lld");
    add(&args, "-o");
    add(&args, output);
    add_all(&args, objects->items, objects->count);
    static const char *const flags[] = {"-Wl,--gc-sections", "-Wl,--build-id=md5", "-Wl,--whole-archive"};
    add_all(&args, flags, sizeof flags / sizeof flags[0]);
    add(&args, tools);
    add(&args, "-Wl,--no-whole-archive");
    add(&args, "-Wl,--start-group");
    add_glob(&args, path("%s/lib/libclang[A-Z]*.a", llvm, ""));
    add_glob(&args, path("%s/lib/libLLVM*.a", llvm, ""));
    static const char *const more[] = {"libclangdSupport.a", "libclangdRemoteIndex.a", "libclangmain.a", "liblldELF.a",
                                       "liblldCommon.a"};
    for (size_t i = 0; i < sizeof more / sizeof more[0]; i++)
        add_glob(&args, path("%s/lib/%s", llvm, more[i]));
    add_glob(&args, path("%s/lib/liblldb[A-Z]*.a", llvm, ""));
    add_glob(&args, path("%s/lib/libsmithlldb.a", llvm, ""));
    static const char *const system[] = {"-Wl,--end-group", "-Wl,-Bstatic", "-lstdc++", "-Wl,-Bdynamic", "-lm",
                                         "-lpthread",       "-ldl",         "-lrt"};
    add_all(&args, system, sizeof system / sizeof system[0]);
    run(args.items);
}

// The folder bin/ of a stage: smith, and the links smith-cc and smith-c++ that act as `smith cc` and `smith c++`.
static void make_bin(const char *folder, const char *smith) {
    run((const char *[]){"mkdir", "-p", folder, nullptr});
    run((const char *[]){"cp", smith, path("%s/smith", folder, ""), nullptr});
    run((const char *[]){"ln", "-sf", "smith", path("%s/smith-cc", folder, ""), nullptr});
    run((const char *[]){"ln", "-sf", "smith", path("%s/smith-c++", folder, ""), nullptr});
}

int main(int argc, char **argv) {
    const char *cc = getenv("CC") != nullptr && *getenv("CC") != '\0' ? getenv("CC") : "cc";
    if (getcwd(cwd, sizeof cwd) == nullptr) die("cannot tell the current folder", nullptr);
    if (!exists("scripts/seed_linux.c")) die("run this from the root of a clone of smith", nullptr);
    run((const char *[]){"mkdir", "-p", ".smith/seed/a", ".smith/seed/b", nullptr});
    FILE *ignore = fopen(".smith/.gitignore", "w"); // as smith writes it: nothing in .smith/ is committed
    if (ignore == nullptr || fputs("*\n", ignore) < 0 || fclose(ignore) != 0) die("cannot write", ".smith/.gitignore");
    build_llvm(cc);
    write_headers(cc);
    printf("seed: stage A with %s\n", identity);
    fflush(stdout);
    Args objects = {};
    compile_smith(&objects);
    Args flags = {};
    smith_flags(&flags, false);
    Args stage_a = objects;
    add(&stage_a, compile(path("%s/bin/clang", llvm, ""), "smith.c", "a", "smith", &flags));
    add(&stage_a, compile_stand_ins());
    link_smith(&stage_a, ".smith/seed/a/smith");
    make_bin(".smith/seed/a/bin", ".smith/seed/a/smith");
    printf("seed: the libraries, compiled by stage A\n");
    fflush(stdout);
    write_protocol();
    Args flags_b = {};
    smith_flags(&flags_b, true);
    Args stage_b = objects;
    add(&stage_b, compile(path("%s/bin/clang", llvm, ""), "smith.c", "b", "smith", &flags_b));
    for (size_t i = 0; i < sizeof libraries / sizeof libraries[0]; i++) {
        add(&stage_b, compile(".smith/seed/a/bin/smith-cc", path("lib/smith/%s.smith", libraries[i], ""), "b",
                              libraries[i], &flags_b));
    }
    link_smith(&stage_b, ".smith/seed/b/smith");
    make_bin(".smith/seed/b/bin", ".smith/seed/b/smith");
    printf("seed: stage B is .smith/seed/b/bin/smith; running its smith build\n");
    char search[path_max * 2];
    const char *old_path = getenv("PATH");
    snprintf(search, sizeof search, "%s/.smith/seed/b/bin:%s", cwd, old_path != nullptr ? old_path : "/usr/bin:/bin");
    setenv("PATH", search, 1);
    unsetenv("CC"); // the compiler of the seed is not the build's: that is smith-cc, from stage B's bin/
    char *build[argc + 2];
    build[0] = (char *)".smith/seed/b/bin/smith";
    build[1] = (char *)"build";
    for (int i = 1; i < argc; i++)
        build[i + 1] = argv[i];
    build[argc + 1] = nullptr;
    fflush(stdout);
    execv(build[0], build);
    die("cannot run", build[0]);
}
