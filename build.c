// The build of smith, run with `smith build`. It replaces the Makefile and runs the commands the Makefile ran.
//
// smith is built by its previous release (knowledge/smith-bootstrap.md): smith.c is C23 and its libraries are .smith
// modules, compiled by the smith that runs this build (`smith-cc`, the default CC), so a smith with the token hook and
// the LLVM of `smith build llvm` is needed. A fresh clone with no smith builds and runs scripts/stage0.c, which
// downloads the newest release of an ancestor commit and runs its `smith build`.
//
// The verbs (smith test, smith check, smith fmt, smith lint, smith docs, smith prove, smith run) mean the same here as
// in every project and read what this file declares: the tests below are the tests, the compiles are what lint and
// prove look at (sources that are never built but should be linted are declared with smith_compile_command). Targets of
// smith build: the default, text-exhaustive, headers, install (-DCC=...,
// -DPREFIX=...), release (CI only), llvm and clean.
#include <smith/build.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// Everything the targets share.
typedef struct BuildPlatform BuildPlatform;

typedef struct {
    const BuildPlatform *platform; // what differs between macOS and Linux
    const char *cc;
    const char *const *cflags;
    const char *binary; // the smith this build makes
    const char *prefix;
    const char *werror;
    const char *llvm;    // the LLVM install smith links clang from, so that it compiles in its own process; else ""
    const char *llvm_id; // the identity of that clang (scripts/llvm.sh): its version and a hash of what is linked
    const char *cxx;     // compiles the shims
    const char *const *headers;  // the headers no smith_object makes (build_header_steps), which smith.c includes
    const char *const *protocol; // the generated protocol code (build_protocol_steps), which smith.c includes
    bool coverage;     // COVERAGE=1: smith.c and the linked libraries count the lines they run (the check that records,
                       // #456)
    bool lldb;         // the install has lldb (scripts/llvm.sh, the lldb step): smith has `smith dbg` and `smith dap`
    const char *prove; // the folder with Z3 and Alive2 beside the LLVM install (build_prove_libs_make), else ""
    bool linkable;     // the install has what smith links (else the default target stops and says `smith build llvm`)
    bool hook;         // the install has the token hook (patches/clang-token-hook.patch): smith compiles .smith files
    const char
        *enzyme; // the folder with libEnzymeStatic-23.a (build_enzyme_make): smith differentiates at link time, else ""
} BuildConfig;

// What smith's own link differs in, macOS and Linux (issue #305, knowledge/platform-layer.md): the one table of it.
struct BuildPlatform {
    const char *whole_start;        // before an archive linked whole: libsmithtools.a, whose tweaks nothing names
    const char *whole_end;          // and after it ("" when the start names the archive itself)
    const char *group_start;        // around the static archives of LLVM, clang, lld and lldb, which name each other
    const char *lld;                // lld's library for the platform's object format
    const char *const *link_flags;  // smith's link: drop what nothing reaches, export only main
    const char *enzyme_export;      // exports Enzyme's llvmGetPassPluginInfo, which shim/lld.cpp's plugin option finds
    const char *const *prove_group; // around the prover's archives, which name each other ({start, end}; none on macOS)
    const char *const *deployment;  // every compile of smith itself: the oldest OS it runs on (macOS 26), else nothing
    const char *const *lldb_libraries;   // what lldb's host layer uses
    const char *const *system_libraries; // last: the C++ library and the platform's own
    const char *const *pack_libraries;   // scripts/pack.c's (libcompression on macOS)
    const char *pack_runtime;            // the files of the LLVM install, besides clang's headers, that are packed
    bool lldb_server; // smith is lldb-server too (shim/lldb_server.cpp): macOS's lldb runs the system's debugserver
};

static const BuildPlatform build_platform_macos = {
    .whole_start = "-Wl,-force_load,",
    .whole_end = "",
    .group_start = "",
    .lld = "liblldMachO.a",
    .link_flags =
        (const char *const[]){"-mmacosx-version-min=26.0", "-Wl,-dead_strip", "-Wl,-exported_symbol,_main", nullptr},
    .enzyme_export = "-Wl,-exported_symbol,_llvmGetPassPluginInfo",
    .prove_group = (const char *const[]){nullptr},
    // smith runs on macOS 26 and newer, built on any newer one (#802): a system function newer than 26 is called only
    // where it exists, and one called bare is an error here, whatever WERROR says.
    .deployment = (const char *const[]){"-mmacosx-version-min=26.0", "-Werror=unguarded-availability-new", nullptr},
    .lldb_libraries = (const char *const[]){"-ledit", "-framework", "Foundation", "-framework", "CoreFoundation",
                                            "-framework", "CoreServices", "-framework", "Security", nullptr},
    .system_libraries = (const char *const[]){"-lc++", "-lm", "-lcompression", nullptr},
    .pack_libraries = (const char *const[]){"-lcompression", nullptr},
    .pack_runtime = "lib/clang/*/lib/darwin/*osx* lib/libLTO.dylib",
};

// On Linux the archives are linked in a group (ELF's linker reads each archive once, in order); libstdc++ is linked
// statically, so smith needs no GLIBCXX version of the machine it runs on; the build-id names its programs folder.
static const BuildPlatform build_platform_linux = {
    .whole_start = "-Wl,--whole-archive",
    .whole_end = "-Wl,--no-whole-archive",
    .group_start = "-Wl,--start-group",
    .lld = "liblldELF.a",
    .link_flags = (const char *const[]){"-Wl,--gc-sections", "-Wl,--build-id=md5", nullptr},
    .enzyme_export = "-Wl,--export-dynamic-symbol=llvmGetPassPluginInfo",
    .prove_group = (const char *const[]){"-Wl,--start-group", "-Wl,--end-group", nullptr},
    .deployment = (const char *const[]){nullptr},
    .lldb_libraries = (const char *const[]){nullptr},
    .system_libraries = (const char *const[]){"-Wl,--end-group", "-Wl,-Bstatic", "-lstdc++", "-Wl,-Bdynamic", "-lm",
                                              "-lpthread", "-ldl", "-lrt", nullptr},
    .pack_libraries = (const char *const[]){nullptr},
    .pack_runtime = "lib/clang/*/lib/*-linux-gnu/*",
    .lldb_server = true,
};

// Whether this build runs on Linux (issue #305): smith build llvm has its own recipe there (scripts/llvm_linux.c)
// and its own packages; everything else is macOS's.
// Transitional (the lag rule, skill smith-dev-build): this file is compiled by the smith that builds it, whose
// <smith/build.h> still has the old shapes (`smith_package` returns the path, `smith_lines` the lines), and later by a
// smith whose header has the results (`smith_build_PackageResult`, `smith_build_Lines`). The two helpers below read
// either through the value's bytes: a plain pointer is the old shape; a result is its tag (0 is `ok`) then, at the
// size of a pointer, the payload (`.ok`, or `.error` whose message is the second word of a PackageError). Replace them
// with the typed code once a release of main has the results, and delete this block.
static const void *build_unwrap(SmithBuild *b, const void *value, size_t size, const char *what, const char *name) {
    (void)b;
    const char *bytes = value;
    if (size == sizeof(void *)) return *(const void *const *)bytes;
    if (*(const int *)bytes != 0) {
        const char *message =
            size > 2 * sizeof(void *) ? *(const char *const *)(bytes + 2 * sizeof(void *)) : "missing";
        fprintf(stderr, "smith: %s %s: %s\n", what, name, message);
        exit(2);
    }
    return *(const void *const *)(bytes + sizeof(void *));
}

// A fetch that fails (not a wrong hash) leaves its url in .smith/unreachable: `smith lint` reads it to skip, by name,
// the files that wait for a package the network does not give (a cloud sandbox), and CI, which has the network, fails.
// The result is read by its layout, as build_unwrap does: the tag, then the error's kind and message.
static void build_note_unreachable(const void *value, size_t size, const char *url) {
    const char *bytes = value;
    if (size <= sizeof(void *) || *(const int *)bytes == 0) return;
    if (*(const int *)(bytes + sizeof(void *)) != SmithPackageKind_Fetch) return;
    FILE *file = fopen(".smith/unreachable", "w");
    if (file == nullptr) return;
    fprintf(file, "%s\n", url);
    fclose(file);
}

// The path of a package, or the build fails with the error's message: smith's own packages are fixed in this file, so
// a fetch that fails is a failure of the build, not something to recover from.
static const char *build_package_path(SmithBuild *b, const SmithPackage *package) {
    __auto_type found = smith_package(b, package);
    build_note_unreachable(&found, sizeof found, package->url);
    return build_unwrap(b, &found, sizeof found, "package", package->url);
}

// The lines of a file, or the build fails: smith's own build reads only files it has just made.
static const char *const *build_lines(SmithBuild *b, const char *path) {
    __auto_type read = smith_lines(b, path);
    return (const char *const *)build_unwrap(b, &read, sizeof read, "cannot read", path);
}

static bool build_is_linux(SmithBuild *b) {
    return strcmp(b->os, "Linux") == 0;
}

static bool build_is_arm(SmithBuild *b) {
    return strcmp(b->arch, "arm64") == 0 || strcmp(b->arch, "aarch64") == 0;
}

static const BuildPlatform *build_platform(SmithBuild *b) {
    return build_is_linux(b) ? &build_platform_linux : &build_platform_macos;
}

// A make variable set with ?=: -DNAME=value, else the environment, else the fallback.
static const char *build_variable(SmithBuild *b, const char *name, const char *fallback, const char *description) {
    return smith_option_value(b, name, smith_env(b, name, fallback), description);
}

// The flags of a .smith source: the build's C flags, the include folder of the generated headers, and C2y (as `smith
// cc` compiles a module, so `_Countof` works).
static const char *const *build_smith_flags(SmithBuild *b, const BuildConfig *config) {
    return smith_cat(b, config->cflags,
                     (const char *[]){"-I", "include", "-I", ".smith/include", "-std=gnu2y", nullptr}, nullptr);
}

// The protocol code smith's debugger server stands on, generated from the official schemas (scripts/protocol.c,
// knowledge/protocol-generation.md): DAP's and MCP's, vendored under schemas/ with their licenses. The generated files
// are build outputs, never committed: each protocol is a step of the build graph that writes them into
// .smith/protocol/, and every compile that includes them waits for it.
typedef struct {
    const char *name;    // dap or mcp: the prefix of the generated names and files
    const char *schema;  // the vendored schema
    const char *license; // and its license
    const char *source;  // for the header of every generated file
} BuildProtocol;

static const BuildProtocol build_protocols[] = {
    {"dap", "schemas/dap/debugAdapterProtocol.json", "schemas/dap/LICENSE",
     "debugAdapterProtocol.json of microsoft/debug-adapter-protocol v1.71.0"},
    {"mcp", "schemas/mcp/schema.json", "schemas/mcp/LICENSE",
     "schema/2026-07-28/schema.json of modelcontextprotocol/modelcontextprotocol, release 2026-07-28"},
};
constexpr size_t build_protocol_count = sizeof build_protocols / sizeof build_protocols[0];

static constexpr char build_library_folder[] = "lib/smith"; // the libraries written in .smith

// The generator is a program of smith's own C (scripts/protocol.c, on lib/json.c), built here and not shipped: like
// scripts/pack.c, a tool of the build. arena, contract, text and mem are the modules of smith, compiled from their
// sources. Returns the program's step.
static SmithStep build_protocol_program(SmithBuild *b, const BuildConfig *config, const char *folder) {
    const char *const sources[] = {"scripts/protocol.c",
                                   smith_fmt(b, "%s/arena.smith", build_library_folder),
                                   smith_fmt(b, "%s/contract.smith", build_library_folder),
                                   smith_fmt(b, "%s/text.smith", build_library_folder),
                                   smith_fmt(b, "%s/mem.smith", build_library_folder),
                                   "lib/json.c",
                                   "lib/trace.c"};
    SmithObject objects[7] = {};
    const SmithObject *linked[8] = {};
    for (size_t i = 0; i < 7; i++) {
        objects[i] = (SmithObject){
            .compiler = config->cc,
            .source = sources[i],
            .output = smith_fmt(b, "%s/protocol-%zu.o", folder, i),
            .flags = smith_cat(b, config->cflags, (const char *[]){"-I", "include", "-I", ".smith/include", nullptr},
                               nullptr),
        };
        if (strstr(sources[i], ".smith") != nullptr) objects[i].flags = build_smith_flags(b, config);
        smith_object(b, &objects[i]);
        linked[i] = &objects[i];
    }
    SmithProgram program = {.linker = config->cc, .output = smith_fmt(b, "%s/protocol", folder), .objects = linked};
    smith_program(b, &program);
    return program.step;
}

// The steps that write .smith/protocol/<name>.[ch], LICENSE-<name>.txt and test_protocol_<name>.c from the vendored
// schema; returns their outputs, which smith.c and the tests that include the code wait for. The generator writes a
// file only when its bytes change, so a build that regenerates nothing rebuilds nothing.
static const char *const *build_protocol_steps(SmithBuild *b, const BuildConfig *config, const char *folder) {
    SmithStep program = build_protocol_program(b, config, folder);
    const char *const *outputs = smith_list(b, nullptr);
    for (size_t i = 0; i < build_protocol_count; i++) {
        const BuildProtocol *protocol = &build_protocols[i];
        const char *name = protocol->name;
        SmithStep step = {
            .command = smith_list(b, program.outputs[0], name, protocol->schema, protocol->license, protocol->source,
                                  ".smith/protocol", ".smith/protocol", nullptr),
            .inputs = smith_list(b, protocol->schema, protocol->license, nullptr),
            .implicit_inputs = program.outputs,
            .outputs =
                smith_list(b, smith_fmt(b, ".smith/protocol/%s.h", name), smith_fmt(b, ".smith/protocol/%s.c", name),
                           smith_fmt(b, ".smith/protocol/LICENSE-%s.txt", name),
                           smith_fmt(b, ".smith/protocol/test_protocol_%s.c", name), nullptr),
            .description = smith_fmt(b, "protocol code %s", name),
            .restat = true,
        };
        smith_step(b, &step);
        outputs = smith_cat(b, outputs, step.outputs, nullptr);
    }
    return outputs;
}

// The libraries in the tree: lib/smith/<name>.smith, a test (<name>_test.smith) not among them. A file says in itself
// what the build does with it, on a line of its own: `// smith-build: link` (smith_object compiles it and smith links
// it) or `// smith-build: header` (only its header is made here); a file with neither is embedded by smith.c and
// linted unless it says `// smith-build: no lint` (a file with no declaration of its own). Adding a library edits no
// list in this file.
static const char *const *build_library_sources(SmithBuild *b) {
    const char *const *found = smith_glob(b, smith_list(b, smith_fmt(b, "%s/*.smith", build_library_folder), nullptr));
    const char *const *sources = smith_list(b, nullptr);
    for (size_t i = 0; found[i] != nullptr; i++) { // bounded by the tree
        size_t length = strlen(found[i]);
        if (length >= strlen("_test.smith") && strcmp(found[i] + length - strlen("_test.smith"), "_test.smith") == 0)
            continue;
        sources = smith_cat(b, sources, smith_list(b, found[i], nullptr), nullptr);
    }
    return sources;
}

// The names (arena, text) of the libraries whose file has the mark on a line of its own.
static const char *const *build_marked(SmithBuild *b, const char *mark) {
    const char *const *sources = build_library_sources(b);
    const char *const *names = smith_list(b, nullptr);
    for (size_t i = 0; sources[i] != nullptr; i++) {                                           // bounded by the tree
        for (const char *const *line = build_lines(b, sources[i]); *line != nullptr; line++) { // bounded by the file
            if (strcmp(*line, mark) != 0) continue;
            const char *base = sources[i] + strlen(build_library_folder) + 1;
            names = smith_cat(b, names, smith_list(b, smith_fmt(b, "%.*s", (int)(strlen(base) - 6), base), nullptr),
                              nullptr);
            break;
        }
    }
    return names;
}

// The headers of the libraries written in .smith (lib/smith/<name>.smith, the module smith::<name>) and of smith::build
// are build outputs, never committed (knowledge/smith-module-headers.md): `.smith/include/smith/<name>.h`, written by
// the smith that compiles smith (CC, the previous release: the lag rule) from the module's source. A module that a
// smith_object compiles (marked `// smith-build: link`) has its header step from smith_object; the others are made
// here, and are inputs of smith.c, which includes them. The installed smith writes the headers of its own embedded
// sources first (`smith build` does, before this file runs), so a first build has them for build.c itself.
static const char *const *build_header_modules(SmithBuild *b) {
    return build_marked(b, "// smith-build: header");
}

// The compiler's own file, which a header step lists as an input: it is the smith that writes the header.
static const char *build_compiler_file(SmithBuild *b, const BuildConfig *config) {
    if (strchr(config->cc, '/') != nullptr) return config->cc;
    return smith_capture_or(b, (const char *[]){"which", config->cc, nullptr}, config->cc);
}

// The headers a header step waits for: a module's header pass reads the headers of the modules it imports, so each
// `import smith::<name>;` of its source that names another of the header modules makes that header an input of the
// step (the others are written first by smith itself, smith_write_lang_headers, which orders them the same way).
static const char *const *build_header_imports(SmithBuild *b, const char *source, const char *self) {
    const char *const *inputs = smith_list(b, nullptr);
    const char *const *modules = build_header_modules(b);
    const char *lines =
        smith_capture_or(b, (const char *[]){"grep", "-h", "^[ \t]*import smith::", source, nullptr}, "");
    for (const char *at = strstr(lines, "import smith::"); at != nullptr; at = strstr(at + 1, "import smith::")) {
        const char *name = at + strlen("import smith::");
        size_t length = strcspn(name, ";\n");
        for (size_t i = 0; modules[i] != nullptr; i++) { // bounded by the tree
            if (strlen(modules[i]) != length || strncmp(modules[i], name, length) != 0) continue;
            if (strcmp(modules[i], self) == 0) continue;
            inputs = smith_cat(b, inputs, smith_list(b, smith_fmt(b, ".smith/include/smith/%s.h", modules[i]), nullptr),
                               nullptr);
        }
    }
    return inputs;
}

// Makes the steps of the headers no smith_object makes; returns their outputs.
static const char *const *build_header_steps(SmithBuild *b, const BuildConfig *config) {
    const char *compiler = build_compiler_file(b, config);
    const char *const *outputs = smith_list(b, nullptr);
    const char *const *modules = build_header_modules(b);
    for (size_t i = 0; modules[i] != nullptr; i++) {
        const char *name = modules[i];
        const char *header = smith_fmt(b, ".smith/include/smith/%s.h", name);
        const char *source = smith_fmt(b, "%s/%s.smith", build_library_folder, name);
        SmithStep step = {
            .command = smith_list(b, config->cc, "--smith-header", source, "-o", header, nullptr),
            .inputs = smith_list(b, source, nullptr),
            .implicit_inputs =
                smith_cat(b, smith_list(b, compiler, nullptr), build_header_imports(b, source, name), nullptr),
            .outputs = smith_list(b, header, nullptr),
            .description = smith_fmt(b, "header %s", source),
            .restat = true, // the header is written only when its bytes change
        };
        smith_step(b, &step);
        outputs = smith_cat(b, outputs, step.outputs, nullptr);
    }
    return outputs;
}

// The libraries of the LLVM install that the in-process clang and the tools link: libsmithtools.a first, in whole,
// which holds the mains of clang-format, clang-tidy and clangd and what they need patched (scripts/llvm.sh), and so is
// found before libclangTidyMain.a's, then all of clang's and of LLVM's, clangd's other libraries (libclangd[A-Z]*
// matches none of them) and libclangmain.a, which holds clang's main. The linker's -dead_strip drops what the driver,
// the compiler and the targets do not reach, so the list is not worked out by hand.
static const char *const *build_clang_libraries(SmithBuild *b, const BuildConfig *config) {
    const char *llvm = config->llvm;
    const BuildPlatform *platform = config->platform;
    const char *const *found = smith_glob(b, (const char *[]){smith_fmt(b, "%s/lib/libclang[A-Z]*.a", llvm),
                                                              smith_fmt(b, "%s/lib/libLLVM*.a", llvm), nullptr});
    const char *tools = smith_fmt(b, "%s/lib/libsmithtools.a", llvm);
    const char *const *first = platform->whole_end[0] == '\0'
                                   ? smith_list(b, smith_fmt(b, "%s%s", platform->whole_start, tools), nullptr)
                                   : smith_list(b, platform->whole_start, tools, platform->whole_end, nullptr);
    const char *const *group =
        smith_when(b, platform->group_start[0] != '\0', (const char *[]){platform->group_start, nullptr});
    const char *const *more = smith_list(
        b, smith_fmt(b, "%s/lib/libclangdSupport.a", llvm), smith_fmt(b, "%s/lib/libclangdRemoteIndex.a", llvm),
        smith_fmt(b, "%s/lib/libclangmain.a", llvm), smith_fmt(b, "%s/lib/%s", llvm, platform->lld),
        smith_fmt(b, "%s/lib/liblldCommon.a", llvm), nullptr);
    return smith_cat(b, first, group, found, more, nullptr);
}

// lldb's libraries, when the install has them (scripts/llvm.sh copies the static ones of its build into the install):
// the plugins and the core, libsmithlldb.a with the SB API and the two mains, and what lldb's host layer uses.
static const char *const *build_lldb_libraries(SmithBuild *b, const BuildConfig *config) {
    if (!config->lldb) return smith_list(b, nullptr);
    const char *const *found =
        smith_glob(b, (const char *[]){smith_fmt(b, "%s/lib/liblldb[A-Z]*.a", config->llvm), nullptr});
    return smith_cat(b, found, smith_list(b, smith_fmt(b, "%s/lib/libsmithlldb.a", config->llvm), nullptr),
                     config->platform->lldb_libraries, nullptr);
}

// Enzyme (build_enzyme_make), before LLVM's libraries: shim/lld.cpp registers it with the linker's LTO pipeline.
static const char *const *build_enzyme_libraries(SmithBuild *b, const BuildConfig *config) {
    if (config->enzyme[0] == '\0') return smith_list(b, nullptr);
    return smith_list(b, smith_fmt(b, "%s/lib/libEnzymeStatic-23.a", config->enzyme), nullptr);
}

// The flags of the shims that read clang's AST and of their precompiled header, which must be the same ones. The LLVM
// headers are system headers, so the build's warnings stay smith's own.
static const char *const *build_shim_flags(SmithBuild *b, const BuildConfig *config) {
    return smith_cat(
        b,
        (const char *[]){"-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-fno-exceptions", "-fno-rtti",
                         "-D__STDC_CONSTANT_MACROS", "-D__STDC_FORMAT_MACROS", "-D__STDC_LIMIT_MACROS", "-isystem",
                         smith_fmt(b, "%s/include", config->llvm), "-isystem", "shim/lldb", nullptr},
        smith_when(b, config->werror[0] != '\0', (const char *[]){config->werror, nullptr}),
        config->platform->deployment, smith_when(b, config->hook, (const char *[]){"-DSMITH_HOOK", nullptr}),
        smith_when(b, config->enzyme[0] != '\0', (const char *[]){"-DSMITH_ENZYME", nullptr}), nullptr);
}

// The shims whose compile is mostly clang's and LLVM's headers (knowledge/precompiled-shim-headers.md): they are
// compiled with the header shim/pch.h precompiled (build_shim_pch). The rest are small (`lsp` and `clangd` measured
// slower with it: loading the header costs more than their includes).
static bool build_shim_uses_pch(const char *name) {
    const char *const names[] = {"hook", "tidy", "affine", "effects_infer"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (strcmp(name, names[i]) == 0) return true;
    }
    return false;
}

// shim/pch.h compiled to `<folder>/shim.pch` as one more object of the graph: its command holds the compiler and the
// flags (the identity of LLVM is in the path of both, and an LLVM is never edited in place, so a new one makes it
// again), its inputs are the compiler's file and, from its depfile, shim/pch.h (the system headers are not in a
// depfile), and the shims that use it depend on it.
// `-fpch-instantiate-templates` puts the templates' instantiations in the header too: the part of the saving that
// matters, since the headers' parse alone is a third of the compile.
// `-fvalidate-ast-input-files-content` (clang's own flag: the driver drops it; here and where it is used) stores each
// input's hash, so a shim/pch.h whose time changed and whose text did not (a checkout that rewrote it) still matches
// the header.
static const char *build_shim_pch(SmithBuild *b, const BuildConfig *config, const char *folder) {
    static SmithObject pch; // the build keeps the pointer
    pch = (SmithObject){
        .compiler = config->cxx,
        .source = "shim/pch.h",
        .output = smith_fmt(b, "%s/shim.pch", folder),
        .flags = smith_cat(b, build_shim_flags(b, config),
                           (const char *[]){"-fpch-instantiate-templates", "-Xclang",
                                            "-fvalidate-ast-input-files-content", "-x", "c++-header", nullptr},
                           // hook's coverage flags (build_shim) are in the header's too, so the two agree.
                           smith_when(b, config->coverage,
                                      (const char *[]){"-fprofile-instr-generate", "-fcoverage-mapping", nullptr}),
                           nullptr),
    };
    smith_object(b, &pch);
    return pch.output;
}

// shim/clang.cpp, which gives smith.c clang's main. `pch` is the precompiled header, or nullptr.
static void build_shim(SmithBuild *b, const BuildConfig *config, const char *folder, const char *source,
                       const char *name, const char *pch, SmithObject *shim) {
    bool uses_pch = pch != nullptr && build_shim_uses_pch(name);
    *shim = (SmithObject){
        .compiler = config->cxx,
        .source = source,
        .output = smith_fmt(b, "%s/%s_shim.o", folder, name),
        .flags = smith_cat(
            b, build_shim_flags(b, config),
            smith_when(b, uses_pch,
                       (const char *[]){"-include-pch", pch, "-Xclang", "-fvalidate-ast-input-files-content", nullptr}),
            // The token hook counts the lines it runs too (COVERAGE=1): the records then select the
            // tests that ran a function of it, and a change to shim/hook.cpp no longer selects every
            // test (#638).
            smith_when(b, config->coverage && strcmp(name, "hook") == 0,
                       (const char *[]){"-fprofile-instr-generate", "-fcoverage-mapping", nullptr}),
            nullptr),
        .implicit_inputs = smith_when(b, uses_pch, (const char *[]){pch, nullptr}),
    };
    smith_object(b, shim);
}

// The prover (`smith prove`): shim/prove*.cpp, which need RTTI and exceptions for Alive2, and the libraries they link
// before LLVM's: Alive2's (build_prove_libs_make builds them beside the LLVM install, which has RTTI and exceptions on)
// and Z3. Alive2's namespace smt is renamed, because Z3's own smt::context has the same name and
// the linker would merge the two.
typedef struct {
    SmithObject objects[4];
    const char *const *libraries;
} BuildProver;

static void build_prover(SmithBuild *b, const BuildConfig *config, const char *folder, BuildProver *prover) {
    const char *const flags_head[] = {"-std=c++20",
                                      "-O1",
                                      "-g",
                                      "-Wall",
                                      "-Wextra",
                                      "-frtti",
                                      "-fexceptions",
                                      "-D__STDC_CONSTANT_MACROS",
                                      "-D__STDC_FORMAT_MACROS",
                                      "-D__STDC_LIMIT_MACROS",
                                      "-Dsmt=alive2_smt",
                                      "-isystem",
                                      nullptr};
    const char *const *flags =
        smith_cat(b, flags_head,
                  smith_list(b, smith_fmt(b, "%s/include", config->llvm), "-isystem",
                             smith_fmt(b, "%s/include/alive2", config->prove), "-isystem",
                             smith_fmt(b, "%s/include", config->prove), nullptr),
                  smith_when(b, config->werror[0] != '\0', (const char *[]){config->werror, nullptr}),
                  config->platform->deployment, nullptr);
    const char *const names[] = {"prove", "prove_ir", "prove_alive", "prove_bitwuzla"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        prover->objects[i] = (SmithObject){
            .compiler = config->cxx,
            .source = smith_fmt(b, "shim/%s.cpp", names[i]),
            .output = smith_fmt(b, "%s/%s.o", folder, names[i]),
            .flags = flags,
            .implicit_inputs = (const char *[]){"shim/prove.hpp", nullptr},
        };
        smith_object(b, &prover->objects[i]);
    }
    const char *const alive[] = {"llvm_util", "tools", "ir", "smt", "util"};
    const char *const solver[] = {"bitwuzla", "bitwuzlabv", "bitwuzlabb", "bitwuzlals",
                                  "bzlarng",  "bzlautil",   "cadical"};
    const char *const *libraries = smith_list(b, nullptr);
    for (size_t i = 0; i < sizeof alive / sizeof alive[0]; i++) {
        libraries =
            smith_cat(b, libraries,
                      smith_list(b, smith_fmt(b, "%s/lib/libalive2_%s.a", config->prove, alive[i]), nullptr), nullptr);
    }
    for (size_t i = 0; i < sizeof solver / sizeof solver[0]; i++) {
        libraries = smith_cat(
            b, libraries, smith_list(b, smith_fmt(b, "%s/lib/lib%s.a", config->prove, solver[i]), nullptr), nullptr);
    }
    // GMP and MPFR are Bitwuzla's: built static beside the rest (build_bitwuzla_make).
    prover->libraries = smith_cat(b, libraries,
                                  smith_list(b, smith_fmt(b, "%s/lib/libz3.a", config->prove),
                                             smith_fmt(b, "%s/lib/libmpfr.a", config->prove),
                                             smith_fmt(b, "%s/lib/libgmp.a", config->prove), nullptr),
                                  nullptr);
}

// What the driver looks up beside its program in the LLVM install, packed (scripts/pack.c) into the file smith.c
// embeds, compressed: clang's resource directory (builtin headers, sanitizer ignore lists, the macOS compiler-rt
// libraries) and libLTO.dylib, which the system linker loads for -flto. Nothing else of the install is needed. The list
// is sorted, so the same install makes the same pack.
static const char *build_resource_pack(SmithBuild *b, const BuildConfig *config, const char *folder) {
    SmithObject tool = {
        .compiler = config->cc,
        .source = "scripts/pack.c",
        .output = smith_fmt(b, "%s/pack.o", folder),
        .flags = config->cflags,
        .implicit_inputs = (const char *[]){"scripts/pack.h", nullptr},
    };
    smith_object(b, &tool);
    SmithProgram program = {
        .linker = config->cc,
        .output = smith_fmt(b, "%s/pack", folder),
        .objects = (const SmithObject *[]){&tool, nullptr},
        .libraries = config->platform->pack_libraries,
    };
    smith_program(b, &program);
    const char *pack = smith_fmt(b, "%s/clang-resource.pack", folder);
    SmithStep step = {
        .command =
            (const char *[]){
                "sh", "-c",
                // NOLINTNEXTLINE(bugprone-suspicious-missing-comma): one script, in two literals
                smith_fmt(b,
                          "root=$PWD && cd \"$0\" && (find lib/clang/*/include lib/clang/*/share -type f; ls %s) | "
                          "LC_ALL=C sort | \"$root/$1\" \"$root/$2\"",
                          config->platform->pack_runtime),
                config->llvm, program.output, pack, nullptr},
        .inputs = smith_cat(b, program.step.outputs,
                            smith_list(b, smith_fmt(b, "%s/lib/smith-clang.id", config->llvm), nullptr), nullptr),
        .outputs = (const char *[]){pack, nullptr},
        .description = "packing clang's resource directory",
    };
    smith_step(b, &step);
    return pack;
}

// runtime/pack_data.c, which embeds the pack and which nothing includes, so that smith.c stays small for the editor.
static void build_pack_data(SmithBuild *b, const BuildConfig *config, const char *folder, const char *pack,
                            SmithObject *data) {
    *data = (SmithObject){
        .compiler = config->cc,
        .source = "runtime/pack_data.c",
        .output = smith_fmt(b, "%s/pack_data.o", folder),
        .flags = smith_cat(b, config->cflags,
                           (const char *[]){smith_fmt(b, "-DSMITH_CLANG_RESOURCE=\"%s\"", pack), nullptr}, nullptr),
        .implicit_inputs = (const char *[]){"scripts/pack.h", pack, nullptr},
    };
    smith_object(b, data);
}

// runtime/commit.c, the commit smith is built from (`smith version` prints it; the Neovim plugin compares it with its
// own), in a translation unit of its own so that a new commit recompiles this file and nothing else. A build outside a
// checkout has no commit and passes no flag.
static SmithObject build_commit_object(SmithBuild *b, const BuildConfig *config, const char *folder) {
    const char *commit = smith_capture_or(b, (const char *[]){"git", "rev-parse", "HEAD", nullptr}, "");
    SmithObject object = {
        .compiler = config->cc,
        .source = "runtime/commit.c",
        .output = smith_fmt(b, "%s/commit.o", folder),
        .flags = smith_cat(
            b, config->cflags,
            smith_when(b, commit[0] != '\0', (const char *[]){smith_fmt(b, "-DSMITH_COMMIT=\"%s\"", commit), nullptr}),
            nullptr),
    };
    smith_object(b, &object);
    return object;
}

static void build_tree_sitter_data(SmithBuild *b, const BuildConfig *config, const char *folder, SmithObject *data);

// Everything smith links beside smith.c when clang is inside: the packed data, the shims of the compiler, the linker
// and the tools, and the prover when its libraries are there. `list` gets pointers to them, ended by nullptr.
typedef struct {
    SmithObject objects[22];
    BuildProver prove;
} BuildShims;

static void build_shims(SmithBuild *b, const BuildConfig *config, const char *folder, const char *pack,
                        BuildShims *shims, const SmithObject **list) {
    build_pack_data(b, config, folder, pack, &shims->objects[0]);
    const char *const sources[] = {"clang",  "lld",           "format",      "tidy",     "clangd",       "symbolize",
                                   "hook",   "lsp",           "lldb",        "lldb_dap", "lldb_explain", "lldb_slices",
                                   "affine", "effects_infer", "lldb_server", "coverage"};
    const char *pch = build_shim_pch(b, config, folder);
    size_t count = 0;
    for (size_t i = 0; i < sizeof sources / sizeof sources[0]; i++) {
        bool is_hook = strcmp(sources[i], "hook") == 0 || strcmp(sources[i], "lsp") == 0 ||
                       strcmp(sources[i], "affine") == 0 || strcmp(sources[i], "effects_infer") == 0;
        bool is_lldb = strncmp(sources[i], "lldb", 4) == 0;
        bool is_server = strcmp(sources[i], "lldb_server") == 0;
        if ((is_hook && !config->hook) || (is_lldb && !config->lldb) || (is_server && !config->platform->lldb_server))
            continue;
        build_shim(b, config, folder, smith_fmt(b, "shim/%s.cpp", sources[i]), sources[i], pch,
                   &shims->objects[count + 1]);
        count += 1;
    }
    build_tree_sitter_data(b, config, folder, &shims->objects[count + 1]);
    count += 1;
    size_t listed = 0;
    for (size_t i = 0; i < count + 1; i++) // the packed data, then the shims, then the parser
        list[listed++] = &shims->objects[i];
    count = listed;
    if (config->prove[0] == '\0') return;
    build_prover(b, config, folder, &shims->prove);
    for (size_t i = 0; i < 4; i++)
        list[count++] = &shims->prove.objects[i];
}

// The links to smith, and with them the outputs of the build: the program, smith-cc and smith-c++.
static const char *const *build_links(SmithBuild *b, const SmithStep *program, const char *folder) {
    const char *links[] = {"smith-cc", "smith-c++"};
    const char *const *outputs = program->outputs;
    for (size_t i = 0; i < sizeof links / sizeof links[0]; i++) {
        SmithStep link = {
            .command = (const char *[]){"ln", "-sf", "smith", "$out", nullptr},
            .inputs = program->outputs,
            .outputs = (const char *[]){smith_fmt(b, "%s/%s", folder, links[i]), nullptr},
        };
        smith_step(b, &link);
        outputs = smith_cat(b, outputs, link.outputs, nullptr);
    }
    return outputs;
}

// What smith.c embeds or includes, which its depfile may not list: a change to any recompiles it.
static const char *const build_smith_inputs[] = {
    "runtime/base.h",
    "runtime/build_runtime.c",
    "runtime/package.c",
    "runtime/gzip.c",
    "runtime/xz.c",
    "runtime/untar.c",
    "runtime/cache.c",
    "runtime/symbolize.c",
    "runtime/explain.c",
    "runtime/platform_macos.c",
    "runtime/platform_linux.c",
    "shim/lldb_explain.h",
    "runtime/mcp.c",
    "runtime/mcp_text.c",
    "runtime/mcp_dap.c",
    "runtime/mcp_show.c",
    "runtime/mcp_tools.c",
    "runtime/mcp_serve.c",
    "runtime/protocol/protocol.h",
    "runtime/protocol/protocol.c",
    "runtime/io.smith",
    "include/smith/json.h",
    "lib/json.c",
    "include/smith/handlers.h",
    "lib/handlers.c",
    "include/smith/http.h",
    "lib/http.c",
    "templates/new-c/main.c",
    "templates/new/gitignore",
    "templates/defaults/clang-format",
    "templates/defaults/clang-tidy",
    "templates/new/claude-settings.json",
    "runtime/lang.h",
    "runtime/lang.c",
    "runtime/lang_out.c",
    "runtime/lang_trivia.c",
    "runtime/lang_decl.c",
    "runtime/lang_names.c",
    "runtime/lang_import.c",
    "runtime/lang_head.c",
    "runtime/lang_generic.c",
    "runtime/lang_constexpr.c",
    "runtime/lang_fields.c",
    "runtime/lang_rfor.c",
    "runtime/lang_sparse.c",
    "runtime/lang_declare.c",
    "runtime/lang_tuple.c",
    "runtime/lang_unused.c",
    "runtime/lang_type.c",
    "runtime/lang_stream.c",
    "runtime/lang_lex.c",
    "runtime/lang_command.c",
    nullptr,
};

// The program, and the same program under two other names, which act by their name as `smith cc` and `smith c++`: for
// what needs one program path (CC=smith-cc, CMake, compile_commands.json). smith-c++ compiles and links C++ with
// clang++. The build folder is named for the OS and architecture, so a binary built on one machine is never mistaken
// for an up-to-date one on another (this folder is shared with a Linux VM as well as macOS). smith.c embeds the build
// header, the runtime and the `smith new` files, so a change to any recompiles it, though the depfile may not list
// them. With an LLVM install (`smith build llvm`), smith.c is built with SMITH_IN_PROCESS, and the shim and clang's
// libraries are linked in, so that smith compiles in its own process. It exports only `main`: every weak definition
// (the C++ inline functions and templates of LLVM, 57,000 of them) that the executable exports is bound by dyld at
// every start against every image loaded, which cost 4 ms in the smith without lldb, and 9 ms more with the frameworks
// lldb loads.
// The libraries smith links, .smith modules compiled by the smith that runs this build (smith.c and the runtime see
// only their headers); text is its own object beside them.
static const char *const *build_linked_libraries(SmithBuild *b) {
    return build_marked(b, "// smith-build: link");
}

// The step that stops the build when the LLVM install is not there, and says what to run: an input of what links it.
// Only the default target runs it, so `smith build llvm` works on a smith with no LLVM install.
static const char *const *build_llvm_required(SmithBuild *b, const BuildConfig *config) {
    if (config->linkable) return smith_list(b, nullptr);
    const char *message =
        smith_fmt(b, "smith: no usable LLVM install at %s: run smith build llvm (9 minutes once)", config->llvm);
    SmithStep stop = {
        .command = (const char *[]){"sh", "-c", "echo \"$0\" >&2; exit 1", message, nullptr},
        .outputs = (const char *[]){"build/llvm-required", nullptr},
    };
    smith_step(b, &stop);
    return stop.outputs;
}

// The prover's archives, in a group on Linux: ELF's linker reads each archive once, in order, and Bitwuzla's name each
// other.
static const char *const *build_prove_group(SmithBuild *b, const BuildConfig *config, const BuildProver *prover) {
    if (prover->libraries == nullptr) return smith_list(b, nullptr);
    const char *const *group = config->platform->prove_group;
    if (group[0] == nullptr) return prover->libraries;
    return smith_cat(b, smith_list(b, group[0], nullptr), prover->libraries, smith_list(b, group[1], nullptr), nullptr);
}

// The objects of the libraries marked `// smith-build: link`; their pointers go to `list`. Returns how many.
static size_t build_library_objects(SmithBuild *b, const BuildConfig *config, const char *folder,
                                    const char *const *flags, const char *const *linked, SmithObject *libraries,
                                    const SmithObject **list) {
    size_t count = 0;
    for (size_t i = 0; linked[i] != nullptr; i++) { // bounded by the tree
        libraries[i] = (SmithObject){
            .compiler = config->cc,
            .source = smith_fmt(b, "%s/%s.smith", build_library_folder, linked[i]),
            .output = smith_fmt(b, "%s/%s.o", folder, linked[i]),
            .flags = flags,
        };
        smith_object(b, &libraries[i]);
        list[count++] = &libraries[i];
    }
    return count;
}

static SmithStep build_programs(SmithBuild *b, const BuildConfig *config, const char *folder) {
    const char *pack = build_resource_pack(b, config, folder);
    const char *const *flags = smith_cat(
        b, config->cflags,
        (const char *[]){"-I", "include", "-I", ".smith/include", "-I", ".smith/protocol", "-I", "runtime/protocol",
                         "-DSMITH_COMMIT_LINKED", "-DSMITH_IN_PROCESS", nullptr},
        (const char *[]){smith_fmt(b, "-DSMITH_CLANG_ID=\"%s\"", config->llvm_id), "-DSMITH_MCP", "-DSMITH_TREE_SITTER",
                         nullptr},
        smith_when(b, config->prove[0] != '\0', (const char *[]){"-DSMITH_PROVE", nullptr}),
        smith_when(b, config->lldb, (const char *[]){"-DSMITH_LLDB", nullptr}),
        smith_when(b, config->hook, (const char *[]){"-DSMITH_HOOK", nullptr}),
        smith_when(b, config->enzyme[0] != '\0', (const char *[]){"-DSMITH_ENZYME", nullptr}),
        smith_when(b, config->coverage, (const char *[]){"-fprofile-instr-generate", "-fcoverage-mapping", nullptr}),
        nullptr);
    // The libraries have smith.c's flags: `smith prove` proves sources whose compile flags are alike together, so a
    // call in smith.c into a library finds the library's body.
    SmithObject object = {
        .compiler = config->cc,
        .source = "smith.c",
        .output = smith_fmt(b, "%s/smith.o", folder),
        .flags = flags,
        .implicit_inputs =
            smith_cat(b, (const char *[]){"scripts/pack.h", nullptr}, build_smith_inputs, build_library_sources(b),
                      config->headers, config->protocol, build_llvm_required(b, config), nullptr),
    };
    smith_object(b, &object);
    SmithObject commit = build_commit_object(b, config, folder);
    BuildShims shims = {};
    const char *const *linked = build_linked_libraries(b);
    size_t linked_count = 0;
    while (linked[linked_count] != nullptr) // bounded by the tree
        linked_count++;
    SmithObject libraries[linked_count + 1];
    // smith's private io layer (runtime/io.smith, issue #903), compiled by the same compiler and linked beside them
    SmithObject runtime_io = {
        .compiler = config->cc,
        .source = "runtime/io.smith",
        .output = smith_fmt(b, "%s/runtime_io.o", folder),
        .flags = flags,
    };
    smith_object(b, &runtime_io);
    // smith.c, the commit, the io layer, the libraries, the shims (a table of 16, the packed data and the parser) and
    // the prover's four, ended by nullptr
    const SmithObject *objects[3 + linked_count + 16 + 2 + 4 + 1];
    for (size_t i = 0; i < sizeof objects / sizeof objects[0]; i++)
        objects[i] = nullptr;
    objects[0] = &object;
    objects[1] = &commit;
    objects[2] = &runtime_io;
    size_t count = 3;
    count += build_library_objects(b, config, folder, flags, linked, libraries, objects + count);
    build_shims(b, config, folder, pack, &shims, objects + count);
    SmithProgram program = {
        .linker = config->cc,
        .output = smith_fmt(b, "%s/smith", folder),
        .flags = smith_cat(
            b, config->platform->link_flags,
            smith_when(b, config->enzyme[0] != '\0', (const char *[]){config->platform->enzyme_export, nullptr}),
            smith_when(b, config->coverage, (const char *[]){"-fprofile-instr-generate", nullptr}), nullptr),
        .objects = objects,
        .libraries = smith_cat(b, build_prove_group(b, config, &shims.prove), build_enzyme_libraries(b, config),
                               build_clang_libraries(b, config), build_lldb_libraries(b, config),
                               config->platform->system_libraries, nullptr),
    };
    smith_program(b, &program);
    smith_default(b, build_links(b, &program.step, folder));
    return program.step;
}

static const char *build_test_header(SmithBuild *b, const char *source) {
    // front/ast.smith is the module front::ast, whose header is front/ast.h: the folder stays in the name.
    if (strncmp(source, "front/", strlen("front/")) == 0)
        return smith_fmt(b, ".smith/include/%s.h", smith_replace_extension(b, source, ""));
    const char *root_less = source + strlen(strncmp(source, "tests/", strlen("tests/")) == 0 ? "tests/" : "lib/");
    return smith_fmt(b, ".smith/include/%s.h", smith_replace_extension(b, root_less, ""));
}

// The headers of all the tests are one step, `smith-cc --smith-headers`: each is a few ms of work but a process costs
// about 10 ms to start, so a step per header made a build that changed the smith (all of them run again, to find
// their bytes the same) cost 2 s of CPU (#639). It is restat: a header whose bytes did not change leaves its compile
// alone.
static const char *const *build_test_header_step(SmithBuild *b, const char *smith_cc, const char *const *flags,
                                                 const char *const *tests) {
    const char *const *pairs = smith_list(b, nullptr);
    const char *const *outputs = smith_list(b, nullptr);
    for (size_t i = 0; tests[i] != nullptr; i++) {
        const char *header = build_test_header(b, tests[i]);
        pairs = smith_cat(b, pairs, smith_list(b, tests[i], header, nullptr), nullptr);
        outputs = smith_cat(b, outputs, smith_list(b, header, nullptr), nullptr);
    }
    SmithStep write = {
        .command = smith_cat(b, smith_list(b, smith_cc, "--smith-headers", nullptr), flags,
                             smith_list(b, "--", nullptr), pairs, nullptr),
        .inputs = tests,
        .implicit_inputs = smith_list(b, smith_cc, nullptr),
        .outputs = outputs,
        .description = "headers of the tests",
        .restat = true, // written only when its bytes change
    };
    smith_step(b, &write);
    return outputs;
}

// The modules of tests/*.smith and the *_test.smith beside the libraries in lib/, for lint: each one's header
// (.smith/include/<name>.h, which the tests that import `scratch` read) and an object nobody asks for, both made by the
// smith under test (the one this build links: a test uses a feature at once). They are plain steps that wait for the
// smith-cc link, so a fresh build folder's first build makes them as its second does. smith_object would put each
// module's header in the check that every compile of a module waits for, and the libraries' compiles, which that link
// waits for, would then wait for these headers: a cycle. Returns what lint needs built (smith_needs reads the inputs of
// a source's compile): the headers and smith-cc.
static const char *const *build_test_modules(SmithBuild *b, const BuildConfig *config, const char *folder) {
    const char *smith_cc = smith_fmt(b, "%s/smith-cc", folder);
    const char *const *flags = build_smith_flags(b, config);
    const char *const *tests =
        smith_glob(b, smith_list(b, "tests/*.smith", "front/*.smith", "lib/*_test.smith",
                                 smith_fmt(b, "%s/*_test.smith", build_library_folder), nullptr));
    const char *const *headers = build_test_header_step(b, smith_cc, flags, tests);
    for (size_t i = 0; tests[i] != nullptr; i++) {
        const char *header = headers[i];
        const char *object = smith_fmt(b, "%s/lint/%zu.o", folder, i);
        // The front end's tests and its emitter (their own runner, build_front_units) import LLVM's C API.
        const char *const *llvm_headers =
            smith_when(b,
                       strncmp(tests[i], "tests/front_", strlen("tests/front_")) == 0 ||
                           strncmp(tests[i], "front/", strlen("front/")) == 0,
                       smith_list(b, "-I", smith_fmt(b, "%s/include", config->llvm), nullptr));
        SmithStep compile = {
            .command = smith_cat(b, smith_list(b, smith_cc, nullptr), flags, llvm_headers,
                                 smith_list(b, "-I", ".smith/include", "-c", tests[i], "-o", object, nullptr), nullptr),
            .inputs = smith_list(b, tests[i], nullptr),
            .implicit_inputs = smith_list(b, smith_cc, header, nullptr),
            .outputs = smith_list(b, object, nullptr),
            .description = smith_fmt(b, "compile %s", tests[i]),
        };
        smith_step(b, &compile);
        smith_compile_command(
            b, smith_cc, tests[i],
            smith_cat(b, flags, llvm_headers, smith_list(b, "-I", ".smith/include", "-fdefer-ts", nullptr), nullptr));
    }
    return smith_cat(b, smith_list(b, smith_cc, nullptr), headers, nullptr);
}

// The template of `smith new` (templates/new/*.smith) is a program of its own whose modules import one another, so each
// one's header is written for lint, into a folder of its own (`main`, `primes` and `primes_test` are names a test may
// use), and each is declared with the flags that read it: without them an importer lowers the template's `primes` with
// no header and names a capability by its prefixed C name. Returns the headers, which lint builds first.
static const char *const *build_template_modules(SmithBuild *b, const BuildConfig *config, const char *folder,
                                                 const char *const *needs) {
    const char *smith_cc = smith_fmt(b, "%s/smith-cc", folder);
    const char *const *flags = build_smith_flags(b, config);
    const char *const *modules = smith_glob(b, smith_list(b, "templates/new/*.smith", nullptr));
    for (size_t i = 0; modules[i] != nullptr; i++) {
        const char *name = smith_replace_extension(b, modules[i] + strlen("templates/new/"), "");
        const char *header = smith_fmt(b, ".smith/template-new/%s.h", name);
        SmithStep write = {
            .command = smith_cat(b, smith_list(b, smith_cc, "--smith-header", modules[i], "-o", header, nullptr), flags,
                                 smith_list(b, "-I", ".smith/template-new", nullptr), nullptr),
            .inputs = smith_list(b, modules[i], nullptr),
            .implicit_inputs = smith_list(b, smith_cc, nullptr),
            .outputs = smith_list(b, header, nullptr),
            .description = smith_fmt(b, "header %s", modules[i]),
            .restat = true,
        };
        smith_step(b, &write);
        smith_compile_command(
            b, smith_cc, modules[i],
            smith_cat(b, flags,
                      smith_list(b, "-I", ".smith/template-new", "-I", ".smith/include", "-fdefer-ts", nullptr),
                      nullptr));
        needs = smith_cat(b, needs, write.outputs, nullptr);
    }
    return needs;
}

// The other two C files are not part of the program, but compiling them in a plain build checks that they build, and
// gives the editor their flags in compile_commands.json. They take the flags lint gives them: the runtime reads
// <smith/build.h>, and the stub is each stub compiler's source, named by -DNAME.
static void build_other_objects(SmithBuild *b, const BuildConfig *config, const char *folder,
                                const char *const *lint_needs) {
    SmithObject runtime = {
        .compiler = config->cc,
        .source = "runtime/build_runtime.c",
        .output = smith_fmt(b, "%s/build_runtime.o", folder),
        .flags =
            smith_cat(b, config->cflags, (const char *[]){"-I", "include", "-I", ".smith/include", nullptr}, nullptr),
    };
    smith_object(b, &runtime);
    SmithObject stub = {
        .compiler = config->cc,
        .source = "tests/stub.c",
        .output = smith_fmt(b, "%s/stub.o", folder),
        .flags = smith_cat(b, config->cflags, (const char *[]){"-DNAME=\"stub\"", nullptr}, nullptr),
        .implicit_inputs = lint_needs, // not read by the compile: `smith lint` builds what a source's compile waits for
    };
    smith_object(b, &stub);
    smith_default(b, smith_cat(b, runtime.step.outputs, stub.step.outputs, nullptr));
}

// Sources the build never compiles on their own, which `smith lint` (and the editor) still read with the flags the
// scripts give them: the libraries and their tests. tests/stub.c is built (build_other_objects) with its -D.
static void build_unbuilt_sources(SmithBuild *b, const BuildConfig *config) {
    const char *const *flags = smith_cat(b, config->cflags,
                                         smith_list(b, "-I", "include", "-I", ".smith/include", "-I", ".smith/protocol",
                                                    "-I", "runtime/protocol", nullptr),
                                         nullptr);
    const char *const *sources =
        smith_glob(b, smith_list(b, "lib/*.c", "tests/*.c", "scripts/defer_init.c", "scripts/header_tool.c", nullptr));
    for (size_t i = 0; sources[i] != nullptr; i++) {
        // stub.c is built (build_other_objects) with its -D, tree_sitter.c is the grammar's, and random.c includes the
        // raw lowering of random.smith, which tests/random_c_test.smith writes when it runs
        if (strcmp(sources[i], "tests/stub.c") == 0 || strcmp(sources[i], "tests/tree_sitter.c") == 0 ||
            strcmp(sources[i], "tests/random.c") == 0)
            continue;
        smith_compile_command(b, config->cc, sources[i], flags);
    }
    // The modules are linted as written, and `.smith` is C2y (as `smith cc` compiles it), so `_Countof` works.
    const char *const *smith_flags = smith_cat(b, flags, smith_list(b, "-std=gnu2y", nullptr), nullptr);
    const char *const *library_sources = build_library_sources(b);
    const char *const *unlinted = build_marked(b, "// smith-build: no lint");
    for (size_t i = 0; library_sources[i] != nullptr; i++) { // bounded by the tree
        bool skip = false;
        for (size_t j = 0; unlinted[j] != nullptr; j++)
            skip =
                skip || strcmp(smith_fmt(b, "%s/%s.smith", build_library_folder, unlinted[j]), library_sources[i]) == 0;
        if (!skip) smith_compile_command(b, config->cc, library_sources[i], smith_flags);
    }
    smith_compile_command(b, config->cc, "runtime/io.smith", smith_flags);
    // The protocol code's hand-written base and its tests' support; the generated files are not linted.
    smith_compile_command(b, config->cc, "runtime/protocol/protocol.c", flags);
    smith_compile_command(b, config->cc, "tests/protocol/protocol_support.c",
                          smith_cat(b, flags, smith_list(b, "-I", "runtime/protocol", nullptr), nullptr));
}

// smith's own tests of behavior, in `.smith` on its test runner (issue #31): every `*_test.smith` (beside the library
// it tests in `lib/`, or in `tests/`) linked into one runner, built once under AddressSanitizer and UBSan, whose
// workers run the tests in process, one per core, where a script starts smith for each case. It is a project of its own
// (tests/units/build.c, in `<folder>/units-project`, whose `tests` and `lib` are links to ours) built and run by the
// smith under test, so the libraries and headers it compiles against are that smith's, whatever smith runs this
// build.c: adding a library needs no two-step install. `units-build` builds the runner, `test-units` runs it with
// `smith test`. Needs a smith that compiles
// `.smith` (the stage0 has none). A test that needs a build or a process of its own (a stop, a sanitizer's catch) stays
// in its script. Returns the runner's path.
static const char *build_units(SmithBuild *b, const BuildConfig *config, const char *binary, const char *folder) {
    static const char setup[] =
        "repo=$PWD; p=$1/units-project; mkdir -p \"$p\" && ln -sfn \"$repo/tests\" \"$p/tests\" && ln -sfn "
        "\"$repo/lib\" \"$p/lib\" && "
        "{ cmp -s tests/units/build.c \"$p/build.c\" || cp tests/units/build.c \"$p/build.c\"; } && cd \"$p\" && ";
    const char *self = smith_fmt(b, "$repo/%s", binary);
    const char *environment =
        smith_fmt(b, "PATH=\"$repo/%s:$PATH\" SMITH_SELF=\"%s\" SMITH_REPO=\"$repo\"", folder, self);
    // A run with COVERAGE=1 (#456): the runner gives every test a folder of profiles, merges them as the test
    // ends, and the records are joined into .smith/coverage/coverage.tsv when the run is over, red or green.
    const char *measure =
        smith_fmt(b, "%s SMITH_COVERAGE_DIR=\"$repo/.smith/coverage\" SMITH_COVERAGE_BINARY=\"%s\"", environment, self);
    const char *join = smith_fmt(b,
                                 "; rc=$?; cd \"$repo\" && \"%s\" test --coverage join .smith/coverage "
                                 "\"$(git rev-parse HEAD)\" .smith/coverage/coverage.tsv; exit $rc",
                                 self);
    const char *inputs[] = {binary, smith_fmt(b, "%s/smith-cc", folder), nullptr};
    SmithStep build_step = {
        .command =
            (const char *[]){"sh", "-c", smith_fmt(b, "%s env %s \"%s\" build build/tests", setup, environment, self),
                             "sh", folder, nullptr},
        .inputs = inputs,
    };
    smith_task(b, "units-build", &build_step);
    // The arguments of `smith test` (--test a,b from --changed, #441) go to the runner of the units project.
    const char *const *given = smith_args(b);
    const char *quoted[256];
    size_t quoted_count = 0;
    // SMITH_TEST_STATS=1 (CI) adds the runner's --stats: a line for each test's CPU time, in the log (#360).
    if (getenv("SMITH_TEST_STATS") != nullptr) quoted[quoted_count++] = "'--stats'";
    for (size_t i = 0; given[i] != nullptr && quoted_count < 255; i++)
        quoted[quoted_count++] = smith_fmt(b, "'%s'", given[i]);
    quoted[quoted_count] = nullptr;
    SmithStep run = {
        .command = (const char *[]){"sh", "-c",
                                    smith_fmt(b, "%s%s env %s \"%s\" test %s%s",
                                              config->coverage ? "rm -rf \"$PWD/.smith/coverage\"; " : "", setup,
                                              config->coverage ? measure : environment, self,
                                              smith_join(b, quoted, " "), config->coverage ? join : ""),
                                    "sh", folder, nullptr},
        .inputs = (const char *[]){"units-build", nullptr},
        .test = true,
    };
    smith_task(b, "test-units", &run);
    return smith_fmt(b, "%s/units-project/build/tests", folder);
}

// The front end's tests (issue #1115, track B of #1112): every front/*.smith and tests/front_*_test.smith linked with
// LLVM's C API into a second runner, beside the units runner (which would be relinked against static LLVM on every
// test change). A project of its own like units (tests/front_units/build.c, in `<folder>/front-project`, whose `front`,
// `tests` and `lib` are links to ours), built and run by the smith under test, with SMITH_LLVM naming the LLVM install
// that smith links. `front-build` builds the runner, `test-front` runs it with `smith test`. Needs a smith that
// compiles `.smith`. Returns nothing: no script waits on it.
static void build_front_units(SmithBuild *b, const BuildConfig *config, const char *binary, const char *folder) {
    static const char setup[] =
        "repo=$PWD; p=$1/front-project; mkdir -p \"$p\" && ln -sfn \"$repo/tests\" \"$p/tests\" && ln -sfn "
        "\"$repo/lib\" \"$p/lib\" && ln -sfn \"$repo/front\" \"$p/front\" && "
        "{ cmp -s tests/front_units/build.c \"$p/build.c\" || cp tests/front_units/build.c \"$p/build.c\"; } && "
        "cd \"$p\" && ";
    const char *self = smith_fmt(b, "$repo/%s", binary);
    const char *environment =
        smith_fmt(b, "PATH=\"$repo/%s:$PATH\" SMITH_SELF=\"%s\" SMITH_REPO=\"$repo\" SMITH_LLVM=\"%s\"", folder, self,
                  config->llvm);
    const char *inputs[] = {binary, smith_fmt(b, "%s/smith-cc", folder), nullptr};
    SmithStep build_step = {
        .command =
            (const char *[]){"sh", "-c", smith_fmt(b, "%s env %s \"%s\" build build/tests", setup, environment, self),
                             "sh", folder, nullptr},
        .inputs = inputs,
    };
    smith_task(b, "front-build", &build_step);
    const char *const *given = smith_args(b);
    const char *quoted[256];
    size_t quoted_count = 0;
    if (getenv("SMITH_TEST_STATS") != nullptr) quoted[quoted_count++] = "'--stats'";
    // --partial: `--test a,b` (from --changed) names tests of every runner, so a selection with none of the front
    // runner's is nothing to run here, not an error; a name no runner knows still fails in the units runner.
    quoted[quoted_count++] = "'--partial'";
    for (size_t i = 0; given[i] != nullptr && quoted_count < 255; i++)
        quoted[quoted_count++] = smith_fmt(b, "'%s'", given[i]);
    quoted[quoted_count] = nullptr;
    SmithStep run = {
        .command = (const char *[]){"sh", "-c",
                                    smith_fmt(b, "%s env %s \"%s\" test %s", setup, environment, self,
                                              smith_join(b, quoted, " ")),
                                    "sh", folder, nullptr},
        .inputs = (const char *[]){"front-build", nullptr},
        .test = true,
    };
    smith_task(b, "test-front", &run);
}

// The soundness harnesses of `smith prove` on 8 seeds of 50000 inputs each (issue #64): the units runner, with the
// smith under test first on PATH and as SMITH_SELF. args are smith's path and the runner's, both relative to the root.
static int build_prove_sound_full(SmithRun *run) {
    char root[PATH_MAX];
    if (getcwd(root, sizeof root) == nullptr) return 1;
    const char *binary = run->args[0];
    const char *slash = strrchr(binary, '/');
    int folder_length = slash != nullptr ? (int)(slash - binary) : 0;
    const char *path = getenv("PATH");
    char path_entry[2 * PATH_MAX];
    char self_entry[2 * PATH_MAX];
    char repo_entry[2 * PATH_MAX];
    char runner[2 * PATH_MAX];
    snprintf(path_entry, sizeof path_entry, "PATH=%s/%.*s:%s", root, folder_length, binary,
             path != nullptr ? path : "");
    snprintf(self_entry, sizeof self_entry, "SMITH_SELF=%s/%s", root, binary);
    snprintf(repo_entry, sizeof repo_entry, "SMITH_REPO=%s", root);
    snprintf(runner, sizeof runner, "%s/%s", root, run->args[1]);
    const char *const argv[] = {runner, "--full", "--test", "*prove_sound_*", nullptr};
    const char *const environment[] = {path_entry, self_entry, repo_entry, "SMITH_OUTPUT=full", nullptr};
    return smith_run_program(run, argv, environment);
}

// The tests of smith (`smith test` runs them): the units runner's `test-units`; returns the list, which the install
// waits for. The runner's steps get SMITH_OUTPUT=full from `smith check` and `smith test` beneath them (#78), so
// `smith build install` (which sets nothing) runs the same tests as `smith check`. The quiet form is tested with the
// variable unset (tests/run_routing_test.smith).
static const char *const *build_test(SmithBuild *b, const BuildConfig *config, const char *binary, const char *folder) {
    const char *units = config->hook ? build_units(b, config, binary, folder) : "";
    if (config->hook) build_front_units(b, config, binary, folder);
    const char *const *tasks = smith_when(b, units[0] != '\0', smith_list(b, "test-units", "test-front", nullptr));
    // Every float through the parser, against strtof: minutes on all cores, so it is not part of `test`.
    SmithStep exhaustive = {
        .command =
            (const char *[]){
                "sh", "-c",
                "work=${TMPDIR:-/tmp}/smith-text-$$.noindex; mkdir \"$work\"; trap 'rm -rf \"$work\"' "
                "EXIT; export XDG_CACHE_HOME=$work/xdg-cache; "
                "\"$0\" cc -std=gnu23 -O2 -Wall -Wextra -Werror -I include -I .smith/include tests/text_exhaustive.c "
                "\"$1\" -pthread -o \"$work/exhaustive\" || exit 1; start=$(date +%s); "
                "\"$work/exhaustive\"; status=$?; "
                "echo \"exhaustive: exit $status in $(($(date +%s) - start)) s\"; exit $status",
                binary, smith_fmt(b, "%s/text.smith", "lib/smith"), nullptr},
        .inputs = (const char *[]){binary, nullptr},
    };
    smith_task(b, "text-exhaustive", &exhaustive);
    // The soundness harnesses of `smith prove` on 8 seeds of 50000 inputs each (issue #64).
    SmithStep sound_full = {
        .run = build_prove_sound_full,
        .args = smith_list(b, binary, units, nullptr),
        .inputs = (const char *[]){binary, units[0] != '\0' ? "units-build" : binary, nullptr},
    };
    smith_task(b, "prove-sound-full", &sound_full);
    return tasks;
}

// Copies smith, once its tests pass, to $PREFIX/bin, with the links smith-cc and smith-c++ (and without the old
// smith++), where projects find it as CC. A link to the build folder would
// hand every project each half-finished build. The copy goes to a temporary name and the rename replaces the binary in
// one step, so a build running meanwhile never sees half a file.
// -DNOTESTS (CI's install job: the check that came first already ran the suite on this tree) installs the binary
// without running the tests; a local `smith build install` runs them.
static void build_install(SmithBuild *b, const BuildConfig *config, const char *binary, const char *const *tested) {
    bool skip_tests = smith_option(b, "NOTESTS", "install without running the tests first (CI's install job)");
    const char *script = "mkdir -p \"$0/bin\" && cp \"$1\" \"$0/bin/smith.tmp\" && "
                         "mv \"$0/bin/smith.tmp\" \"$0/bin/smith\" && ln -sf smith \"$0/bin/smith-cc\" && "
                         "ln -sf smith \"$0/bin/smith-c++\" && rm -f \"$0/bin/smith++\" && "
                         "{ [ ! -f \"${1%/*}/libsmith_handlers.dylib\" ] || "
                         "cp \"${1%/*}/libsmith_handlers.dylib\" \"$0/bin/\"; }";
    SmithStep install = {
        .command = (const char *[]){"sh", "-c", script, config->prefix, binary, nullptr},
        .inputs = skip_tests ? (const char *[]){binary, nullptr} : tested,
    };
    smith_task(b, "install", &install);
}

// `smith build release` (issue #111, knowledge/release.md): publishes the built smith as the GitHub Release
// `stage0-<commit>` and prunes the old ones. scripts/release.sh refuses unless HEAD is main's tip (or CI's push to
// main). No test or install is an input: CI's check and install jobs come first, and a failure here breaks neither.
static void build_release(SmithBuild *b, const char *binary) {
    SmithStep release = {
        .command = (const char *[]){"sh", "scripts/release.sh", "publish", binary, nullptr},
        .description = "publishing the stage0 release",
        .inputs = (const char *[]){binary, nullptr},
    };
    smith_task(b, "release", &release);
}

// The folder of the LLVM configuration under the package cache: one build of 23.1.3 with RTTI and exceptions on (Alive2
// needs them), the token hook (#11), and the prover's libraries beside it. r14 is the fourteenth configuration
// (patches/*.patch; r14 changes `SmithCEval` only (#1018); r12 has no new patch: it is the pack of Linux that gains the
// prover and Enzyme, #820); scripts/llvm.sh has the same name (SMITH_LLVM_NAME overrides both).
static const char *build_llvm_folder(SmithBuild *b) {
    const char *cache = smith_env(b, "XDG_CACHE_HOME", smith_fmt(b, "%s/.cache", smith_env(b, "HOME", "")));
    return smith_fmt(b, "%s/smith/llvm/%s", cache, smith_env(b, "SMITH_LLVM_NAME", "23.1.3-r14"));
}

// Enzyme beside the LLVM configurations, keyed by its version and the configuration it was built against, so that
// building it leaves the LLVM install alone (build_enzyme_make).
static const char *build_enzyme_folder(SmithBuild *b) {
    const char *cache = smith_env(b, "XDG_CACHE_HOME", smith_fmt(b, "%s/.cache", smith_env(b, "HOME", "")));
    return smith_fmt(b, "%s/smith/enzyme/0.0.303-%s", cache, smith_env(b, "SMITH_LLVM_NAME", "23.1.3-r14"));
}

static const char *build_cmake_package(SmithBuild *b) {
    if (build_is_linux(b) && build_is_arm(b)) {
        return build_package_path(
            b, &(SmithPackage){
                   .url = "https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-linux-aarch64.tar.gz",
                   .hash = "sha256:a1b6cc63636a0e55c63257cf3315a8a5f129e42fade25db1afea4ff8ab06f25e"});
    }
    if (build_is_linux(b)) {
        return build_package_path(
            b, &(SmithPackage){
                   .url = "https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-linux-x86_64.tar.gz",
                   .hash = "sha256:e5bb807f7728cb60cd8b27ebc97a2edb469b68655f21e844a600c3575b76f5bb"});
    }
    return build_package_path(
        b, &(SmithPackage){
               .url = "https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-macos-universal.tar.gz",
               .hash = "sha256:4b7b73704b1db9b374e5c9ab8e17ac6148b817b6396ee75cd94a852cbac9d305"});
}

static const char *build_python_package(SmithBuild *b) {
    if (build_is_linux(b) && build_is_arm(b)) {
        return build_package_path(
            b, &(SmithPackage){.url = "https://github.com/astral-sh/python-build-standalone/releases/download/20261020/"
                                      "cpython-3.13.16%2B20261020-aarch64-unknown-linux-gnu-install_only.tar.gz",
                               .hash = "sha256:908e16f2083b59cc1eea70b8afb7995acae3f524b2e26409beae01392e07561b"});
    }
    if (build_is_linux(b)) {
        return build_package_path(
            b, &(SmithPackage){.url = "https://github.com/astral-sh/python-build-standalone/releases/download/20261020/"
                                      "cpython-3.13.16%2B20261020-x86_64-unknown-linux-gnu-install_only.tar.gz",
                               .hash = "sha256:0ad293ecda8ea93a28e4a6b5e9cc20ae74be15aafed75be04418b8f82bd0d0da"});
    }
    return build_package_path(
        b, &(SmithPackage){.url = "https://github.com/astral-sh/python-build-standalone/releases/download/20261020/"
                                  "cpython-3.13.16%2B20261020-aarch64-apple-darwin-install_only.tar.gz",
                           .hash = "sha256:235f3d5b182a171f6564ee98ef2812cfb2da8a73c4a37fca1937b25702f94880"});
}

// ---- The scripts that were shell (#291): each is a function that runs as a step or a task, with no shell. The
// programs run through build_spawn, which is smith_run_program with a folder to run in, a log for a noisy build and the
// environment to add.
extern char **environ;

typedef struct {
    const char *const *argv;
    const char *const *environment; // NAME=value entries, replacing a variable of the same name
    const char *folder;             // run here
    const char *log;                // stdout and stderr go here; on a failure its last lines are printed
    int32_t tail;                   // how many
    bool stdout_only;               // only stdout goes to the log
    bool append;                    // the log is not truncated first
} BuildSpawn;

constexpr size_t build_environment_max = 2048;

static const char *const *build_merged_environment(const char *const *extra) {
    static const char *merged[build_environment_max];
    size_t extra_count = 0;
    while (extra != nullptr && extra[extra_count] != nullptr)
        extra_count++;
    size_t count = 0;
    for (char *const *entry = environ; *entry != nullptr; entry++) {
        bool replaced = false;
        for (size_t i = 0; i < extra_count; i++)
            replaced = replaced || strncmp(*entry, extra[i], strcspn(extra[i], "=") + 1) == 0;
        if (replaced) continue;
        if (count + extra_count + 1 >= build_environment_max) {
            fputs("smith: the environment is too large\n", stderr);
            exit(1);
        }
        merged[count++] = *entry;
    }
    for (size_t i = 0; i < extra_count; i++)
        merged[count++] = extra[i];
    merged[count] = nullptr;
    return merged;
}

// The last `lines` lines of a log, at most its last 16 KiB.
static void build_print_tail(const char *path, int32_t lines) {
    static char text[16384];
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    long start = size > (long)sizeof text - 1 ? size - ((long)sizeof text - 1) : 0;
    fseek(file, start, SEEK_SET);
    size_t used = fread(text, 1, sizeof text - 1, file);
    fclose(file);
    text[used] = '\0';
    const char *from = text + used;
    if (used > 0 && from[-1] == '\n') from--;
    int32_t seen = 0;
    while (from > text && seen < lines) {
        from--;
        seen += *from == '\n' ? 1 : 0;
    }
    if (*from == '\n') from++;
    fputs(from, stderr);
}

static int build_wait(pid_t pid) {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return 127;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 127;
}

// 0 for success; the program's status otherwise, 127 when it cannot start.
static int build_spawn(const BuildSpawn *spawn) {
    fflush(stdout);
    fflush(stderr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (spawn->log != nullptr) {
        int flags = O_WRONLY | O_CREAT | (spawn->append ? O_APPEND : O_TRUNC);
        posix_spawn_file_actions_addopen(&actions, 1, spawn->log, flags, 0644);
        if (!spawn->stdout_only) posix_spawn_file_actions_adddup2(&actions, 1, 2);
    }
    pid_t pid = 0;
    // The child starts in the folder this process is in for the moment of the spawn (this process runs one step and has
    // no other thread), so no deprecated or newer spawn extension is needed.
    char home[PATH_MAX];
    bool moved = spawn->folder != nullptr && getcwd(home, sizeof home) != nullptr && chdir(spawn->folder) == 0;
    int error = spawn->folder != nullptr && !moved
                    ? ENOENT
                    : posix_spawnp(&pid, spawn->argv[0], &actions, nullptr, (char *const *)spawn->argv,
                                   (char *const *)build_merged_environment(spawn->environment));
    if (moved && chdir(home) != 0) error = error != 0 ? error : EIO;
    posix_spawn_file_actions_destroy(&actions);
    int status = error != 0 ? 127 : build_wait(pid);
    if (error != 0) fprintf(stderr, "smith: cannot run %s: %s\n", spawn->argv[0], strerror(error));
    if (status != 0 && spawn->log != nullptr && spawn->tail > 0) build_print_tail(spawn->log, spawn->tail);
    return status;
}

static int build_exec(const char *const *argv) {
    return build_spawn(&(BuildSpawn){.argv = argv});
}

static int build_remove(SmithBuild *b, const char *path) {
    return build_exec(smith_list(b, "/bin/rm", "-rf", path, nullptr));
}

static int build_make_folder(SmithBuild *b, const char *path) {
    return build_exec(smith_list(b, "/bin/mkdir", "-p", path, nullptr));
}

// A copy of a source tree (a package is never written to) that the build may write to: `cp -R from/. to/`.
static int build_copy_tree(SmithBuild *b, const char *from, const char *to) {
    int status =
        build_exec(smith_list(b, "/bin/cp", "-R", smith_fmt(b, "%s/.", from), smith_fmt(b, "%s/", to), nullptr));
    return status != 0 ? status : build_exec(smith_list(b, "/bin/chmod", "-R", "u+w", to, nullptr));
}

static int build_write_text(const char *path, const char *text, mode_t mode) {
    FILE *file = fopen(path, "w");
    if (file == nullptr) return 1;
    bool written = fputs(text, file) >= 0;
    bool closed = fclose(file) == 0;
    return written && closed && chmod(path, mode) == 0 ? 0 : 1;
}

// Replaces `path` by `from` in one step, so no reader sees half a file.
static int build_publish(SmithBuild *b, const char *from, const char *path) {
    int status = build_exec(smith_list(b, "/bin/cp", from, smith_fmt(b, "%s.part", path), nullptr));
    return status != 0 ? status : (rename(smith_fmt(b, "%s.part", path), path) != 0 ? 1 : 0);
}

static const char *build_parent(SmithBuild *b, const char *path) {
    const char *slash = strrchr(path, '/');
    return slash == nullptr ? "." : smith_fmt(b, "%.*s", (int)(slash - path), path);
}

static const char *build_base(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash == nullptr ? path : slash + 1;
}

static const char *build_sdk(SmithRun *run) {
    const char *const *lines = smith_run_lines(run, (const char *[]){"xcrun", "--show-sdk-path", nullptr});
    return lines == nullptr || lines[0] == nullptr ? "" : lines[0];
}

// Ninja from its pinned source, for the builds that `smith build llvm` runs (LLVM's CMake, Z3, Alive2 and Bitwuzla's
// Meson all want one), so that nothing comes from Homebrew: $out, made once. Ninja builds itself with Python and the
// system's C++ compiler, in a copy of the source.
static int build_ninja_bootstrap(SmithBuild *b, const char *src, const char *python, const char *work,
                                 const char *out) {
    int status = build_copy_tree(b, src, work);
    if (status != 0) return status;
    status = build_make_folder(b, build_parent(b, out));
    if (status != 0) return status;
    status = build_spawn(&(BuildSpawn){
        .argv = smith_list(b, smith_fmt(b, "%s/bin/python3", python), "-I", "configure.py", "--bootstrap", nullptr),
        .environment = smith_list(b, "CXX=/usr/bin/clang++", nullptr),
        .folder = work,
        .log = "/dev/null",
        .stdout_only = true});
    return status != 0 ? status : build_publish(b, smith_fmt(b, "%s/ninja", work), out);
}

static int build_ninja_make(SmithRun *run, const char *src, const char *python, const char *out) {
    if (access(out, X_OK) == 0) return 0;
    char work[1024];
    snprintf(work, sizeof work, "%s/ninja-%d.noindex", smith_env(run->b, "TMPDIR", "/tmp"), (int)getpid());
    if (mkdir(work, 0700) != 0) return 1;
    int status = build_ninja_bootstrap(run->b, src, python, work, out);
    build_remove(run->b, work);
    return status;
}

// Enzyme (reverse and forward mode automatic differentiation of LLVM IR), static, against the LLVM install: its
// library into `out`/lib, a folder of its own keyed by the version and the LLVM configuration, so building it never
// touches the LLVM install that other smiths link. Run again with nothing changed, cmake and ninja find nothing to do.
//   -DENZYME_RUNPASS makes the static library hook the optimization pipelines (without it nothing runs Enzyme);
//   ENZYME_CLANG=OFF: no clang plugin, smith runs Enzyme at link time (shim/lld.cpp);
//   patches/enzyme/*.patch go into a copy of the package, as the LLVM patches do.
typedef struct {
    const char *source;
    const char *cmake; // the package with CMake.app
    const char *llvm;  // the install
    const char *ninja;
    const char *out;
} BuildEnzyme;

static int build_enzyme_patched(SmithBuild *b, const char *source, const char *work) {
    const char *patched = smith_fmt(b, "%s/.patched", work);
    if (access(patched, F_OK) == 0) return 0;
    const char *copy = smith_fmt(b, "%s/src", work);
    int status = build_remove(b, copy);
    status = status != 0 ? status : build_make_folder(b, copy);
    status = status != 0 ? status : build_copy_tree(b, source, copy);
    // patch reads -i after -d changes its folder, so each patch is named from the repository's root.
    char root[PATH_MAX];
    if (status == 0 && getcwd(root, sizeof root) == nullptr) status = 1;
    const char *const *patches = smith_glob(b, smith_list(b, "patches/enzyme/*.patch", nullptr));
    for (size_t i = 0; status == 0 && patches[i] != nullptr; i++) {
        const char *patch = smith_fmt(b, "%s/%s", root, patches[i]);
        status = build_exec(smith_list(b, "patch", "-p1", "-s", "-d", copy, "-i", patch, nullptr));
    }
    return status != 0 ? status : build_write_text(patched, "", 0644);
}

static int build_enzyme_make(SmithRun *run, const BuildEnzyme *enzyme) {
    SmithBuild *b = run->b;
    const char *name = build_base(build_parent(b, enzyme->llvm));
    const char *work = smith_fmt(b, "%s/build-%s", build_parent(b, enzyme->out), build_base(enzyme->out));
    const char *sdk = build_sdk(run);
    const char *const environment[] = {smith_fmt(b, "SDKROOT=%s", sdk), nullptr};
    int status = build_make_folder(b, smith_fmt(b, "%s/lib", enzyme->out));
    status = status != 0 ? status : build_make_folder(b, work);
    status = status != 0 ? status : build_enzyme_patched(b, enzyme->source, work);
    if (status != 0) return status;
    const char *const configure[] = {smith_fmt(b, "%s/CMake.app/Contents/bin/cmake", enzyme->cmake),
                                     "-G",
                                     "Ninja",
                                     "-S",
                                     smith_fmt(b, "%s/src/enzyme", work),
                                     "-B",
                                     smith_fmt(b, "%s/build", work),
                                     "-DCMAKE_BUILD_TYPE=Release",
                                     "-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local",
                                     smith_fmt(b, "-DCMAKE_MAKE_PROGRAM=%s", enzyme->ninja),
                                     "-DCMAKE_C_COMPILER=/usr/bin/clang",
                                     "-DCMAKE_CXX_COMPILER=/usr/bin/clang++",
                                     smith_fmt(b, "-DCMAKE_OSX_SYSROOT=%s", sdk),
                                     smith_fmt(b, "-DLLVM_DIR=%s/lib/cmake/llvm", enzyme->llvm),
                                     smith_fmt(b, "-DClang_DIR=%s/lib/cmake/clang", enzyme->llvm),
                                     "-DENZYME_STATIC_LIB=ON",
                                     "-DENZYME_CLANG=OFF",
                                     "-DLLVM_EXTERNAL_LIT=/nonexistent",
                                     "-DCMAKE_CXX_FLAGS=-DENZYME_RUNPASS",
                                     nullptr};
    status = build_spawn(&(BuildSpawn){.argv = configure, .environment = environment});
    if (status != 0) return status;
    const char *const compile[] = {"/usr/bin/nice",   enzyme->ninja, "-C", smith_fmt(b, "%s/build", work),
                                   "EnzymeStatic-23", nullptr};
    status = build_spawn(&(BuildSpawn){.argv = compile, .environment = environment});
    const char *library = smith_fmt(b, "%s/lib/libEnzymeStatic-23.a", enzyme->out);
    status =
        status != 0 ? status : build_publish(b, smith_fmt(b, "%s/build/Enzyme/libEnzymeStatic-23.a", work), library);
    return status != 0 ? status
                       : build_write_text(smith_fmt(b, "%s/lib/smith-enzyme.id", enzyme->out),
                                          smith_fmt(b, "enzyme-0.0.303 llvm-%s\n", name), 0644);
}

// What `smith prove` links beside LLVM: Z3 and the libraries of Alive2 that hold LLVM IR's semantics (ir, smt, util,
// tools and llvm_util; not its translation-validation plugin or its executables), both static, into
// <LLVM folder>/prove/{lib,include}. Alive2 is built against the LLVM install (RTTI and exceptions on).
typedef struct {
    const char *cmake; // the cmake binary
    const char *python;
    const char *ninja;
    const char *llvm; // the install
    const char *cache;
    const char *out;
    const char *sdk;
} BuildProve;

static int build_z3_compile(SmithBuild *b, const BuildProve *prove, const char *z3) {
    const char *const configure[] = {prove->cmake,
                                     "-G",
                                     "Ninja",
                                     "-S",
                                     z3,
                                     "-B",
                                     smith_fmt(b, "%s/z3-build", prove->cache),
                                     "-DCMAKE_BUILD_TYPE=Release",
                                     "-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local",
                                     smith_fmt(b, "-DCMAKE_INSTALL_PREFIX=%s", prove->out),
                                     smith_fmt(b, "-DCMAKE_MAKE_PROGRAM=%s", prove->ninja),
                                     "-DCMAKE_C_COMPILER=/usr/bin/clang",
                                     "-DCMAKE_CXX_COMPILER=/usr/bin/clang++",
                                     smith_fmt(b, "-DCMAKE_OSX_SYSROOT=%s", prove->sdk),
                                     smith_fmt(b, "-DPython3_EXECUTABLE=%s/bin/python3", prove->python),
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
    int status = build_exec(configure);
    const char *const install[] = {"/usr/bin/nice", prove->ninja, "-C", smith_fmt(b, "%s/z3-build", prove->cache),
                                   "install",       nullptr};
    return status != 0 ? status : build_exec(install);
}

// Z3's static initializers. libz3.a has about ten thousand, which dyld would run in every smith process at load (the
// prover is in all of them). They are moved to __DATA,__smith_prove (scripts/defer_init.c, as for the tools in
// scripts/llvm.sh), and shim/prove.cpp runs them when `smith prove` does. The archive is rebuilt from the objects of
// Z3's build, in the order Z3's own archive step used.
static int build_z3_objects(SmithRun *run, const BuildProve *prove, const char *deferred) {
    SmithBuild *b = run->b;
    // ninja's output is one line of more than a megabyte, so it goes to a file and not to a pipe.
    const char *listing = smith_fmt(b, "%s/commands.txt", deferred);
    int listed =
        build_spawn(&(BuildSpawn){.argv = smith_list(b, prove->ninja, "-C", smith_fmt(b, "%s/z3-build", prove->cache),
                                                     "-t", "commands", "libz3.a", nullptr),
                                  .log = listing,
                                  .stdout_only = true});
    if (listed != 0) return listed;
    const char *const *commands = build_lines(b, listing);
    if (commands == nullptr || commands[0] == nullptr) return 1;
    size_t last = 0;
    while (commands[last + 1] != nullptr)
        last++;
    char *line = (char *)commands[last]; // the arena copy: strtok_r cuts it into words
    int count = 0;
    char *state = nullptr;
    for (char *word = strtok_r(line, " ", &state); word != nullptr; word = strtok_r(nullptr, " ", &state)) {
        size_t length = strlen(word);
        if (length < 2 || strcmp(word + length - 2, ".o") != 0) continue;
        count++;
        int status =
            build_spawn(&(BuildSpawn){.argv = smith_list(b, smith_fmt(b, "%s/tools/defer_init", prove->cache),
                                                         smith_fmt(b, "%s/z3-build/%s", prove->cache, word),
                                                         smith_fmt(b, "%s/%05d.o", deferred, count), "prove", nullptr),
                                      .log = "/dev/null",
                                      .stdout_only = true});
        if (status != 0) return status;
    }
    return 0;
}

static int build_z3_defer(SmithRun *run, const BuildProve *prove) {
    SmithBuild *b = run->b;
    const char *deferred = smith_fmt(b, "%s/z3-deferred", prove->cache);
    int status = build_exec(smith_list(b, "/usr/bin/clang", "-std=gnu23", "-O1", "-Wall", "-Wextra", "-Werror",
                                       "scripts/defer_init.c", "-o", smith_fmt(b, "%s/tools/defer_init", prove->cache),
                                       nullptr));
    status = status != 0 ? status : build_remove(b, deferred);
    status = status != 0 ? status : build_make_folder(b, deferred);
    status = status != 0 ? status : build_z3_objects(run, prove, deferred);
    const char *library = smith_fmt(b, "%s/lib/libz3.a", prove->out);
    status = status != 0 ? status : build_remove(b, library);
    const char *const *archive =
        smith_cat(b, smith_list(b, "/usr/bin/ar", "rcs", library, nullptr),
                  smith_glob(b, smith_list(b, smith_fmt(b, "%s/*.o", deferred), nullptr)), nullptr);
    return status != 0 ? status : build_exec(archive);
}

static int build_alive2_compile(SmithBuild *b, const BuildProve *prove, const char *alive2) {
    const char *git = smith_fmt(b, "%s/tools/git", prove->cache);
    // Alive2 finds its version with git, which a package has none of: a stand-in that names the pin. re2c only builds
    // its own `alive` tool, which is not built here.
    if (build_write_text(git, "#!/bin/sh\necho smith-pinned\n", 0755) != 0) return 1;
    const char *const configure[] = {prove->cmake,
                                     "-G",
                                     "Ninja",
                                     "-S",
                                     alive2,
                                     "-B",
                                     smith_fmt(b, "%s/alive2-build", prove->cache),
                                     "-DCMAKE_BUILD_TYPE=Release",
                                     "-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local",
                                     smith_fmt(b, "-DCMAKE_MAKE_PROGRAM=%s", prove->ninja),
                                     "-DCMAKE_C_COMPILER=/usr/bin/clang",
                                     "-DCMAKE_CXX_COMPILER=/usr/bin/clang++",
                                     smith_fmt(b, "-DCMAKE_OSX_SYSROOT=%s", prove->sdk),
                                     "-DCMAKE_CXX_FLAGS=-Wno-error -Dsmt=alive2_smt",
                                     smith_fmt(b, "-DGIT_EXECUTABLE=%s", git),
                                     "-DRE2C=/usr/bin/true",
                                     "-DBUILD_LLVM_UTILS=1",
                                     smith_fmt(b, "-DLLVM_DIR=%s/lib/cmake/llvm", prove->llvm),
                                     smith_fmt(b, "-DZ3_INCLUDE_DIR=%s/include", prove->out),
                                     smith_fmt(b, "-DZ3_LIBRARIES=%s/lib/libz3.a", prove->out),
                                     nullptr};
    int status = build_exec(configure);
    const char *const compile[] = {
        "/usr/bin/nice", prove->ninja, "-C",   smith_fmt(b, "%s/alive2-build", prove->cache), "ir", "smt", "tools",
        "util",          "llvm_util",  nullptr};
    return status != 0 ? status : build_exec(compile);
}

// The libraries and the headers Alive2's own headers include, by the paths they use (ir/state.h, smt/expr.h, ...), with
// the generated version header.
static int build_alive2_install(SmithBuild *b, const BuildProve *prove, const char *alive2) {
    const char *const folders[] = {"ir", "smt", "tools", "util", "llvm_util"};
    const char *headers = smith_fmt(b, "%s/include/alive2", prove->out);
    int status = build_remove(b, headers);
    status = status != 0 ? status : build_make_folder(b, headers);
    for (size_t i = 0; status == 0 && i < sizeof folders / sizeof folders[0]; i++) {
        const char *folder = folders[i];
        status = build_exec(smith_list(b, "/bin/cp", smith_fmt(b, "%s/alive2-build/lib%s.a", prove->cache, folder),
                                       smith_fmt(b, "%s/lib/libalive2_%s.a", prove->out, folder), nullptr));
        status = status != 0 ? status : build_make_folder(b, smith_fmt(b, "%s/%s", headers, folder));
        const char *const *files = smith_glob(b, smith_list(b, smith_fmt(b, "%s/%s/*.h", alive2, folder),
                                                            smith_fmt(b, "%s/%s/*.inc", alive2, folder), nullptr));
        if (status == 0 && files[0] != nullptr)
            status = build_exec(smith_cat(b, smith_list(b, "/bin/cp", nullptr), files,
                                          smith_list(b, smith_fmt(b, "%s/%s/", headers, folder), nullptr), nullptr));
    }
    return status != 0
               ? status
               : build_exec(smith_list(b, "/bin/cp", smith_fmt(b, "%s/alive2-build/version_gen.h", prove->cache),
                                       smith_fmt(b, "%s/", headers), nullptr));
}

static int build_prove_libs_make(SmithRun *run, const char *z3, const char *alive2, const BuildProve *prove) {
    SmithBuild *b = run->b;
    int status = build_make_folder(b, smith_fmt(b, "%s/lib", prove->out));
    status = status != 0 ? status : build_make_folder(b, smith_fmt(b, "%s/include", prove->out));
    status = status != 0 ? status : build_make_folder(b, smith_fmt(b, "%s/tools", prove->cache));
    status = status != 0 ? status : build_z3_compile(b, prove, z3);
    status = status != 0 ? status : build_z3_defer(run, prove);
    status = status != 0 ? status : build_alive2_compile(b, prove, alive2);
    return status != 0 ? status : build_alive2_install(b, prove, alive2);
}

// Bitwuzla (the SMT solver `smith prove` asks first; Z3 only finds the model of a counterexample) and what it needs,
// all static, beside Z3 and Alive2's libraries: <LLVM folder>/prove/{lib,include}. Nothing comes from Homebrew, and
// nothing from the network:
//   - GMP and MPFR are built with their own configure and the system's make, into the output;
//   - Meson would fetch CaDiCaL and symfpu by the hashes and revisions its wrap files pin; the same two archives are
//     packages here (CaDiCaL by the wrap's own hash, symfpu's by the revision's archive) and go where the wraps'
//     downloads would, with the wraps' overlays (subprojects/packagefiles);
//   - Bitwuzla's Meson looks GMP and MPFR up with pkg-config, which is not part of macOS: in the copy of the source it
//   is
//     told where the static libraries are instead.
typedef struct {
    const char *bitwuzla, *meson, *python, *ninja, *gmp, *mpfr, *cadical, *symfpu;
    const char *cache, *out;
    const char *const *environment; // macOS's own tools, our ninja and python
} BuildBitwuzla;

// `./configure && make && make install` in a copy of the package, logged to <name>-build.log.
static int build_autotools(SmithBuild *b, const BuildBitwuzla *bz, const char *name, const char *package,
                           const char *const *configure_flags) {
    const char *folder = smith_fmt(b, "%s/%s-build", bz->cache, name);
    int status = build_remove(b, folder);
    status = status != 0 ? status : build_exec(smith_list(b, "/bin/cp", "-R", package, folder, nullptr));
    status = status != 0 ? status : build_exec(smith_list(b, "/bin/chmod", "-R", "u+w", folder, nullptr));
    if (status != 0) return status;
    const char *log = smith_fmt(b, "%s/%s-build.log", bz->cache, name);
    const char *jobs = smith_fmt(b, "-j%s", smith_capture(b, smith_list(b, "sysctl", "-n", "hw.ncpu", nullptr)));
    const char *const *commands[] = {
        smith_cat(b, smith_list(b, "./configure", smith_fmt(b, "--prefix=%s", bz->out), nullptr), configure_flags,
                  nullptr),
        smith_list(b, "make", jobs, nullptr), smith_list(b, "make", "install", nullptr)};
    for (size_t i = 0; status == 0 && i < 3; i++) {
        status = build_spawn(&(BuildSpawn){.argv = commands[i],
                                           .environment = bz->environment,
                                           .folder = folder,
                                           .log = log,
                                           .tail = 30,
                                           .append = i > 0});
    }
    return status;
}

// GMP and MPFR, in copies of their sources (a package is never written to). MPFR is built against the GMP just made.
static int build_bitwuzla_numbers(SmithBuild *b, const BuildBitwuzla *bz) {
    if (access(smith_fmt(b, "%s/lib/libmpfr.a", bz->out), F_OK) == 0) return 0;
    int status = build_autotools(b, bz, "gmp", bz->gmp,
                                 smith_list(b, "--disable-shared", "--enable-static", "--enable-cxx", nullptr));
    return status != 0 ? status
                       : build_autotools(b, bz, "mpfr", bz->mpfr,
                                         smith_list(b, smith_fmt(b, "--with-gmp=%s", bz->out), "--disable-shared",
                                                    "--enable-static", nullptr));
}

// Meson writes its subprojects into the source tree: build from a copy, with the two subprojects in place.
static int build_bitwuzla_source(SmithBuild *b, const BuildBitwuzla *bz, const char *source) {
    const char *subprojects = smith_fmt(b, "%s/subprojects", source);
    const char *cadical = smith_fmt(b, "%s/cadical-rel-2.1.2", subprojects);
    const char *symfpu = smith_fmt(b, "%s/symfpu", subprojects);
    int status = build_remove(b, source);
    status = status != 0 ? status : build_remove(b, smith_fmt(b, "%s/bitwuzla-build", bz->cache));
    status = status != 0 ? status : build_exec(smith_list(b, "/bin/cp", "-R", bz->bitwuzla, source, nullptr));
    status = status != 0 ? status : build_exec(smith_list(b, "/bin/chmod", "-R", "u+w", source, nullptr));
    status = status != 0 ? status : build_exec(smith_list(b, "/bin/cp", "-R", bz->cadical, cadical, nullptr));
    status = status != 0 ? status : build_copy_tree(b, smith_fmt(b, "%s/packagefiles/cadical", subprojects), cadical);
    status = status != 0 ? status : build_exec(smith_list(b, "/bin/cp", "-R", bz->symfpu, symfpu, nullptr));
    status = status != 0 ? status : build_copy_tree(b, smith_fmt(b, "%s/packagefiles/symfpu", subprojects), symfpu);
    return status != 0 ? status : build_exec(smith_list(b, "/bin/chmod", "-R", "u+w", subprojects, nullptr));
}

// GMP and MPFR by find_library, in the one place Bitwuzla looks them up.
static int build_bitwuzla_find_library(SmithBuild *b, const BuildBitwuzla *bz, const char *source) {
    const char *meson_build = smith_fmt(b, "%s/src/meson.build", source);
    const char *program =
        "s|gmp_dep = dependency\\(.gmp.,.*?static: build_static\\)\\nmpfr_dep = dependency\\(.mpfr.,.*?static: "
        "build_static\\)|gmp_dep = cpp_compiler.find_library(\\x27gmp\\x27, dirs: [\\x27$ENV{LIBDIR}\\x27], static: "
        "true)\\nmpfr_dep = cpp_compiler.find_library(\\x27mpfr\\x27, dirs: [\\x27$ENV{LIBDIR}\\x27], static: "
        "true)|s";
    const char *const environment[] = {smith_fmt(b, "LIBDIR=%s/lib", bz->out), nullptr};
    int status =
        build_spawn(&(BuildSpawn){.argv = smith_list(b, "/usr/bin/perl", "-0pi", "-e", program, meson_build, nullptr),
                                  .environment = environment});
    if (status != 0) return status;
    for (const char *const *line = build_lines(b, meson_build); *line != nullptr; line++) {
        if (strstr(*line, "find_library('gmp'") != nullptr) return 0;
    }
    fputs("bitwuzla: Bitwuzla's src/meson.build changed in a way this function does not know\n", stderr);
    return 1;
}

// Only the libraries: the solver's own program is not wanted.
static int build_bitwuzla_libraries(SmithBuild *b, const BuildBitwuzla *bz, const char *source,
                                    const char *build_folder) {
    const char *const libraries[] = {"subprojects/cadical-rel-2.1.2/src/libcadical.a",
                                     "src/libbzlautil.a",
                                     "src/lib/libbzlarng.a",
                                     "src/lib/libbitwuzlabv.a",
                                     "src/lib/libbitwuzlabb.a",
                                     "src/lib/libbitwuzlals.a",
                                     "src/libbitwuzla.a"};
    const char *const compile[] = {"/usr/bin/nice", bz->ninja,    "-C",         build_folder,
                                   libraries[0],    libraries[1], libraries[2], libraries[3],
                                   libraries[4],    libraries[5], libraries[6], nullptr};
    int status = build_spawn(&(BuildSpawn){.argv = compile,
                                           .environment = bz->environment,
                                           .log = smith_fmt(b, "%s/bitwuzla-build.log", bz->cache),
                                           .tail = 40});
    for (size_t i = 0; status == 0 && i < sizeof libraries / sizeof libraries[0]; i++) {
        status = build_exec(smith_list(b, "/bin/cp", smith_fmt(b, "%s/%s", build_folder, libraries[i]),
                                       smith_fmt(b, "%s/lib/", bz->out), nullptr));
    }
    const char *headers = smith_fmt(b, "%s/include/bitwuzla", bz->out);
    status = status != 0 ? status : build_make_folder(b, headers);
    return status != 0 ? status : build_copy_tree(b, smith_fmt(b, "%s/include/bitwuzla", source), headers);
}

static int build_bitwuzla_make(SmithRun *run, BuildBitwuzla *bz) {
    SmithBuild *b = run->b;
    const char *sdk = build_sdk(run);
    bz->environment =
        smith_list(b, smith_fmt(b, "SDKROOT=%s", sdk),
                   smith_fmt(b, "PATH=%s:%s/bin:/usr/bin:/bin:/usr/sbin:/sbin", build_parent(b, bz->ninja), bz->python),
                   "CC=/usr/bin/clang", "CXX=/usr/bin/clang++", nullptr);
    int status = build_make_folder(b, smith_fmt(b, "%s/lib", bz->out));
    status = status != 0 ? status : build_make_folder(b, smith_fmt(b, "%s/include", bz->out));
    status = status != 0 ? status : build_bitwuzla_numbers(b, bz);
    const char *source = smith_fmt(b, "%s/bitwuzla-src", bz->cache);
    status = status != 0 ? status : build_bitwuzla_source(b, bz, source);
    status = status != 0 ? status : build_bitwuzla_find_library(b, bz, source);
    if (status != 0) return status;
    const char *build_folder = smith_fmt(b, "%s/bitwuzla-build", bz->cache);
    const char *setup_log = smith_fmt(b, "%s/bitwuzla-setup.log", bz->cache);
    const char *const setup[] = {smith_fmt(b, "%s/bin/python3", bz->python),
                                 "-I",
                                 smith_fmt(b, "%s/meson.py", bz->meson),
                                 "setup",
                                 build_folder,
                                 source,
                                 "--buildtype=release",
                                 "--default-library=static",
                                 smith_fmt(b, "--prefix=%s", bz->out),
                                 "--wrap-mode=nodownload",
                                 smith_fmt(b, "-Dc_args=-I%s/include", bz->out),
                                 smith_fmt(b, "-Dcpp_args=-I%s/include", bz->out),
                                 "-Dtesting=disabled",
                                 "-Dunit_testing=disabled",
                                 "-Dpython=false",
                                 "-Ddocs=false",
                                 nullptr};
    status = build_spawn(&(BuildSpawn){.argv = setup, .environment = bz->environment, .log = setup_log, .tail = 30});
    return status != 0 ? status : build_bitwuzla_libraries(b, bz, source, build_folder);
}

// The packages the prover's libraries are built from, pinned by hash. Alive2 is the last commit before it moved to LLVM
// APIs that 23.1.3 does not have (its August 2026 commits to TargetLibraryInfo's getLibFunc and to nofree attributes),
// so it is the newest that builds against LLVM 23.1.3.
static int build_prove_libs_run(SmithRun *run, const char *ninja) {
    SmithBuild *b = run->b;
    const char *z3 = build_package_path(
        b, &(SmithPackage){.url = "https://github.com/Z3Prover/z3/archive/refs/tags/z3-5.3.1.tar.gz",
                           .hash = "sha256:c007089dd8d92298d2a7d14105b14e8dddfbb540062ce471c3111e0741edd8da"});
    const char *alive2 = build_package_path(
        b, &(SmithPackage){.url = "https://github.com/AliveToolkit/alive2/archive/"
                                  "01a5ec45c8152995755f7331827407a9de19f262.tar.gz",
                           .hash = "sha256:ea6d0ad5869a4c863ca537c65fe22e2f1198a5a1b2fc51d3a96b8020cf78cc13"});
    const char *cache = build_llvm_folder(b);
    BuildProve prove = {
        .cmake = smith_fmt(b, "%s/CMake.app/Contents/bin/cmake", build_cmake_package(b)),
        .python = build_python_package(b),
        .ninja = ninja,
        .llvm = smith_fmt(b, "%s/install", cache),
        .cache = cache,
        .out = smith_fmt(b, "%s/prove", cache),
        .sdk = build_sdk(run),
    };
    return build_prove_libs_make(run, z3, alive2, &prove);
}

// Bitwuzla decides the queries (build_bitwuzla_make), with its own dependencies: GMP and MPFR, CaDiCaL and symfpu
// (which Meson would fetch itself), and Meson to build it. All pinned by hash, all built static, none from Homebrew.
static int build_bitwuzla_run(SmithRun *run, const char *ninja) {
    SmithBuild *b = run->b;
    const char *bitwuzla = build_package_path(
        b, &(SmithPackage){.url = "https://github.com/bitwuzla/bitwuzla/archive/refs/tags/0.9.1.tar.gz",
                           .hash = "sha256:42707f38900a20bb18108e426ba667560d1fd2ccce0d4f75aa60439b546488b4"});
    const char *meson = build_package_path(
        b, &(SmithPackage){.url = "https://github.com/mesonbuild/meson/releases/download/1.9.1/meson-1.9.1.tar.gz",
                           .hash = "sha256:4e076606f2afff7881d195574bddcd8d89286f35a17b4977a216f535dc0c74ac"});
    const char *gmp = build_package_path(
        b, &(SmithPackage){.url = "https://ftp.gnu.org/gnu/gmp/gmp-6.3.0.tar.xz",
                           .hash = "sha256:a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898"});
    const char *mpfr = build_package_path(
        b, &(SmithPackage){.url = "https://ftp.gnu.org/gnu/mpfr/mpfr-4.2.2.tar.xz",
                           .hash = "sha256:b67ba0383ef7e8a8563734e2e889ef5ec3c3b898a01d00fa0a6869ad81c6ce01"});
    const char *cadical = build_package_path(
        b, &(SmithPackage){.url = "https://github.com/arminbiere/cadical/archive/rel-2.1.2.tar.gz",
                           .hash = "sha256:292c2bb8d712d6d05fce3d3df63b922b8fa45e03974a79f7bae5bf68c284f131"});
    const char *symfpu = build_package_path(
        b, &(SmithPackage){.url = "https://github.com/martin-cs/symfpu/archive/"
                                  "40bdec00e99f8ea1b96c3dac0a05eed11c541639.tar.gz",
                           .hash = "sha256:ba17877fbf0c851e113fddaab225152f1c0b2044429396b56b2f113832e36ce5"});
    const char *cache = build_llvm_folder(b);
    BuildBitwuzla bitwuzla_build = {.bitwuzla = bitwuzla,
                                    .meson = meson,
                                    .python = build_python_package(b),
                                    .ninja = ninja,
                                    .gmp = gmp,
                                    .mpfr = mpfr,
                                    .cadical = cadical,
                                    .symfpu = symfpu,
                                    .cache = cache,
                                    .out = smith_fmt(b, "%s/prove", cache)};
    return build_bitwuzla_make(run, &bitwuzla_build);
}

// Enzyme v0.0.303 (Apache-2.0 with the LLVM exception), pinned by hash and built static beside the LLVM install by
// build_enzyme_make, with patches/enzyme/*.patch applied to a copy.
static int build_enzyme_run(SmithRun *run, const char *ninja) {
    SmithBuild *b = run->b;
    const char *enzyme = build_package_path(
        b, &(SmithPackage){.url = "https://github.com/EnzymeAD/Enzyme/archive/refs/tags/v0.0.303.tar.gz",
                           .hash = "sha256:0ca3114d008e524cd3b13152bbc172794b20af643d1628161655373eba4554de"});
    BuildEnzyme build = {.source = enzyme,
                         .cmake = build_cmake_package(b),
                         .llvm = smith_fmt(b, "%s/install", build_llvm_folder(b)),
                         .ninja = ninja,
                         .out = build_enzyme_folder(b)};
    return build_enzyme_make(run, &build);
}

// `smith build enzyme`: only Enzyme, for an LLVM that is already built (`smith build llvm` runs it last too).
static int build_enzyme_only(SmithRun *run) {
    int status = build_enzyme_run(run, smith_fmt(run->b, "%s/tools/ninja", build_llvm_folder(run->b)));
    if (status != 0) return status;
    const char *const done[] = {"sh", "-c", ": >\"$0\"", run->outputs[0], nullptr};
    return smith_run_program(run, done, nullptr);
}

// On Linux (issue #305): the same LLVM, built by scripts/llvm_linux.c (a program, compiled here with the smith
// that runs this build), which takes scripts/llvm.sh's arguments with the programs themselves. The prover's libraries
// and Enzyme are not built on Linux yet.
static int build_llvm_run_linux(SmithRun *run, const char *source, const char *ninja, const char *stamp) {
    SmithBuild *b = run->b;
    const char *tool = smith_fmt(b, "%s.tool", run->outputs[0]);
    const char *const compile[] = {smith_env(b, "CC", "smith-cc"), "-std=c23", "-O1", "-x",   "c",
                                   "scripts/llvm_linux.c",         "-o",       tool,  nullptr};
    int status = smith_run_program(run, compile, nullptr);
    if (status != 0) return status;
    const char *const make_llvm[] = {tool,
                                     source,
                                     smith_fmt(b, "%s/bin/cmake", build_cmake_package(b)),
                                     smith_fmt(b, "%s/bin/python3", build_python_package(b)),
                                     stamp,
                                     ninja,
                                     nullptr};
    status = smith_run_program(run, make_llvm, nullptr);
    if (status != 0) return status;
    const char *const done[] = {"cp", stamp, run->outputs[0], nullptr};
    return smith_run_program(run, done, nullptr);
}

// After a build of LLVM (issue #618): every other LLVM folder of the cache goes, and its Enzyme, but the previous
// release's, the one the smith that runs this build links (its identity is what `smith-cc --smith-which` prints), so a
// build of the previous commit still works. scripts/llvm_prune.c does it; when the identity cannot be read nothing is
// removed.
static int build_llvm_prune(SmithRun *run) {
    SmithBuild *b = run->b;
    const char *cc = smith_env(b, "CC", "smith-cc");
    const char *which = smith_capture_or(b, (const char *[]){cc, "--smith-which", nullptr}, "");
    const char *line = strstr(which, "identity ");
    if (line == nullptr) return 0;
    const char *identity = line + strlen("identity ");
    identity = smith_fmt(b, "%.*s", (int)strcspn(identity, "\n"), identity);
    const char *current = build_llvm_folder(b);
    const char *name = strrchr(current, '/') + 1;
    const char *tool = smith_fmt(b, "%s/../llvm_prune", current);
    const char *const compile[] = {cc, "-std=c23", "-O1", "scripts/llvm_prune.c", "-o", tool, nullptr};
    int status = build_exec(compile);
    if (status != 0) return status;
    const char *const prune[] = {tool, smith_fmt(b, "%s/..", current), name, identity, nullptr};
    return build_exec(prune);
}

// scripts/llvm_pack.c (issue #797): the LLVM pack of a configuration is a GitHub Release `llvm-<name>-<os>-<arch>`,
// what smith links and embeds, so that a machine with no LLVM downloads it instead of building it. Compiles the program
// (plain C23, into the cache beside the LLVM folders) and runs it with `arguments` after the cache's smith folder and
// the configuration's name; its exit status.
static int build_llvm_pack_run(SmithRun *run, const char *verb, const char *const *arguments) {
    SmithBuild *b = run->b;
    const char *folder = build_llvm_folder(b);
    const char *home = smith_env(b, "XDG_CACHE_HOME", smith_fmt(b, "%s/.cache", smith_env(b, "HOME", "")));
    const char *cache = smith_fmt(b, "%s/smith", home);
    const char *tool = smith_fmt(b, "%s/llvm/llvm_pack", cache);
    const char *name = strrchr(folder, '/') + 1;
    int status = build_make_folder(b, smith_fmt(b, "%s/llvm", cache));
    if (status != 0) return status;
    const char *const compile[] = {
        smith_env(b, "CC", "smith-cc"), "-std=c23", "-O1", "scripts/llvm_pack.c", "-o", tool, nullptr};
    status = build_exec(compile);
    if (status != 0) return status;
    return build_exec(smith_cat(b, (const char *[]){tool, verb, cache, name, nullptr}, arguments, nullptr));
}

// The fetch path of `smith build llvm` (-Dfetch): `auto` takes the pack when its release exists and builds otherwise,
// `0` always builds, `1` takes the pack or fails. Returns the step's status when the pack was taken (its folders are
// where a build puts them, so nothing else is needed), or -1 when LLVM is to be built.
static int build_llvm_fetch(SmithRun *run) {
    const char *mode = run->args[0];
    if (strcmp(mode, "0") == 0) return -1;
    SmithBuild *b = run->b;
    const char *stamp = smith_fmt(b, "%s.fetch", run->outputs[0]);
    int status = build_llvm_pack_run(run, "fetch", smith_list(b, stamp, nullptr));
    if (status == 0) return build_exec(smith_list(b, "cp", stamp, run->outputs[0], nullptr));
    if (status != 3 || strcmp(mode, "1") == 0) return status != 0 ? status : 1;
    fputs("smith: no LLVM pack to fetch: building LLVM from source (-Dfetch=0 never looks)\n", stderr);
    return -1;
}

// `smith build llvm-pack [-Dout=DIR]` writes the pack of the LLVM that is built into DIR (to measure it); `smith build
// llvm-publish` packs it and publishes the release (CI's llvm workflow), and does nothing when the release exists.
static int build_llvm_pack_task(SmithRun *run) {
    SmithBuild *b = run->b;
    if (strcmp(run->args[0], "publish") == 0) return build_llvm_pack_run(run, "publish", smith_list(b, nullptr));
    return build_llvm_pack_run(run, "pack", smith_list(b, run->args[1], nullptr));
}

// The pinned LLVM (`smith build llvm`): the pack of a release when there is one (build_llvm_fetch), else the sources,
// CMake and Python by hash, built once into the package cache by scripts/llvm.sh, and then the prover's libraries
// beside it (Ninja, Z3, Alive2, Bitwuzla and its dependencies). A .run step, so the packages are fetched only when this
// target runs, not on every smith build.
static int build_llvm_run(SmithRun *run) {
    SmithBuild *b = run->b;
    int fetched = build_llvm_fetch(run);
    if (fetched >= 0) return fetched;
    const char *ninja_source = build_package_path(
        b, &(SmithPackage){.url = "https://github.com/ninja-build/ninja/archive/refs/tags/v1.13.2.tar.gz",
                           .hash = "sha256:974d6b2f4eeefa25625d34da3cb36bdcebe7fbce40f4c16ac0835fd1c0cbae17"});
    const char *source = build_package_path(
        b,
        &(SmithPackage){
            .url =
                "https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.3/llvm-project-23.1.3.src.tar.xz",
            .hash = "sha256:c44186a7762ed28954be72e5ff6df9808e0779d4f1bf014ecc4e7e211d31ee34"});
    const char *folder = build_llvm_folder(b);
    const char *ninja = smith_fmt(b, "%s/tools/ninja", folder);
    const char *stamp = smith_fmt(b, "%s.llvm", run->outputs[0]);
    int status = build_ninja_make(run, ninja_source, build_python_package(b), ninja);
    if (status != 0) return status;
    if (build_is_linux(b)) {
        status = build_llvm_run_linux(run, source, ninja, stamp);
        return status != 0 ? status : build_llvm_prune(run);
    }
    const char *const make_llvm[] = {
        "sh", "scripts/llvm.sh", source, build_cmake_package(b), build_python_package(b), stamp, ninja, nullptr};
    status = smith_run_program(run, make_llvm, nullptr);
    if (status != 0) return status;
    status = build_prove_libs_run(run, ninja);
    if (status != 0) return status;
    status = build_bitwuzla_run(run, ninja);
    if (status != 0) return status;
    status = build_enzyme_run(run, ninja);
    if (status != 0) return status;
    status = build_llvm_prune(run);
    if (status != 0) return status;
    const char *const done[] = {"sh", "-c", "cp \"$1\" \"$2\"", "sh", stamp, run->outputs[0], nullptr};
    return smith_run_program(run, done, nullptr);
}

static void build_llvm(SmithBuild *b) {
    SmithStep llvm = {
        .run = build_llvm_run,
        .args = smith_list(b,
                           smith_option_value(b, "fetch", "auto",
                                              "llvm: auto takes the release's pack when there is one, 0 builds, 1 "
                                              "fetches or fails"),
                           nullptr),
        .description = "fetching or building LLVM 23.1.3 and the prover's libraries (static, from source)",
        .inputs = smith_cat(
            b,
            (const char *[]){"scripts/llvm.sh", "scripts/defer_init.c", "patches/enzyme/quiet-caching-call.patch",
                             "patches/clang-token-hook.patch", "patches/clang-debug-name.patch",
                             "patches/clang-format-smith.patch", "patches/smith-editor-defaults.patch",
                             "patches/smith-format-source-extension.patch", "patches/smith-command-hook.patch",
                             "patches/compiler-rt-xnu-fast-mmap.patch", "patches/smith-lang-format-filescope-if.patch",
                             "scripts/llvm_pack.c", "scripts/sha256.h", nullptr},
            smith_when(b, build_is_linux(b), (const char *[]){"scripts/llvm_linux.c", nullptr}), nullptr),
        .outputs = (const char *[]){"build/llvm.stamp", nullptr},
    };
    smith_step(b, &llvm);
    smith_alias(b, "llvm", llvm.outputs);
    SmithStep pack = {
        .run = build_llvm_pack_task,
        .args = smith_list(b, "pack", smith_option_value(b, "out", "build/llvm-pack", "llvm-pack: where the pack goes"),
                           nullptr),
        .description = "packing the LLVM that is built (what smith links) for a release",
    };
    smith_task(b, "llvm-pack", &pack);
    SmithStep publish = {
        .run = build_llvm_pack_task,
        .args = smith_list(b, "publish", "", nullptr),
        .description = "publishing the LLVM pack as the release llvm-<name>-<os>-<arch>",
    };
    smith_task(b, "llvm-publish", &publish);
    SmithStep enzyme = {
        .run = build_enzyme_only,
        .description = "building Enzyme (static, against the LLVM install)",
        .inputs = (const char *[]){"patches/enzyme/quiet-caching-call.patch", nullptr},
        .outputs = (const char *[]){"build/enzyme.stamp", nullptr},
    };
    smith_step(b, &enzyme);
    smith_alias(b, "enzyme", enzyme.outputs);
}

// `smith build friction -Dsince=2026-10-24` (AGENTS.md, How to work): the friction sections of merged pull requests,
// for the lead's review across PRs. Each pull request's body ends with `## Friction`; this prints that section of every
// PR merged since a date, newest first, each under its PR's number and title.
static int build_friction(SmithRun *run) {
    const char *since = run->args[0];
    if (since[0] == '\0') {
        fputs("usage: smith build friction -Dsince=YYYY-MM-DD\n", stderr);
        return 2;
    }
    const char *filter = ".[] | select(.body | test(\"(?m)^## Friction\")) | \"#\\(.number) \\(.title)\\n\\(.body | "
                         "split(\"## Friction\")[1] | split(\"\\n## \")[0] | ltrimstr(\"\\n\"))\\n\"";
    return build_exec(smith_list(run->b, "gh", "pr", "list", "--state", "merged", "--limit", "200", "--search",
                                 smith_fmt(run->b, "merged:>=%s", since), "--json", "number,title,body", "--jq", filter,
                                 nullptr));
}

// The tree-sitter parser for .smith files (#466): a build output, never committed. A step generates parser.c,
// grammar.json, node-types.json and the runtime's headers into tree-sitter/src/ (gitignored) from
// tree-sitter/grammar.js and tree-sitter-c's, with the tree-sitter CLI; parser.c is compiled and linked into a shared
// library, which smith embeds (runtime/tree_sitter_data.c) and `smith cc --smith-tree-sitter` unpacks for the Neovim
// plugin. Everything is pinned and fetched by smith_package: the CLI by version and the sha256 of its release (a .gz of
// one file, per platform), tree-sitter-c by version and the sha256 of its source archive. The CLI is kept in
// $XDG_CACHE_HOME/smith/tools (else ~/.cache/smith/tools), and is needed only on the machine that builds smith. ABI 15
// is what Neovim 0.12 loads. `smith build tree-sitter -Dcorpus` also runs the grammar's corpus tests.
typedef struct {
    const char *asset; // the release's file, without .gz
    const char *hash;
} BuildTreeSitterPin;

static BuildTreeSitterPin build_tree_sitter_pin(SmithBuild *b) {
    bool arm = build_is_arm(b);
    if (build_is_linux(b))
        return arm ? (BuildTreeSitterPin){"tree-sitter-linux-arm64",
                                          "sha256:404be5d54a4955571c4067c21d4699660eab3a534cefb0659341b207f7139ec5"}
                   : (BuildTreeSitterPin){"tree-sitter-linux-x64",
                                          "sha256:d01042f94ba3d6827ddc9788e235f75d8366431be42bdf90ade534d960c98b7d"};
    return arm ? (BuildTreeSitterPin){"tree-sitter-macos-arm64",
                                      "sha256:46cb97d9a2c52c94e22d9a76c2f1116156b8b61f98360e2a9cce05fcb197cd4d"}
               : (BuildTreeSitterPin){"tree-sitter-macos-x64",
                                      "sha256:b740262cb0f35be64e39b50ad68b020433924a7bcee5ae5c8e40e68d71215647"};
}

static const char *build_tree_sitter_cli(SmithBuild *b) {
    const char *cache = smith_env(b, "XDG_CACHE_HOME", smith_fmt(b, "%s/.cache", smith_env(b, "HOME", "")));
    const char *tools = smith_fmt(b, "%s/smith/tools", cache);
    BuildTreeSitterPin pin = build_tree_sitter_pin(b);
    const char *cli = smith_fmt(b, "%s/%s-0.27.1", tools, pin.asset);
    if (access(cli, X_OK) == 0) return cli;
    const char *source = build_package_path(
        b,
        &(SmithPackage){.url = smith_fmt(
                            b, "https://github.com/tree-sitter/tree-sitter/releases/download/v0.27.1/%s.gz", pin.asset),
                        .hash = pin.hash});
    if (build_make_folder(b, tools) != 0) return nullptr;
    if (build_publish(b, smith_fmt(b, "%s/%s", source, pin.asset), cli) != 0) return nullptr;
    return build_exec(smith_list(b, "/bin/chmod", "+x", cli, nullptr)) == 0 ? cli : nullptr;
}

// args[0] is `generate` (the step that writes tree-sitter/src) or `corpus` (the task: also runs the corpus tests).
static int build_tree_sitter_run(SmithRun *run) {
    SmithBuild *b = run->b;
    if (strcmp(run->args[0], "none") == 0) return 0;
    const char *cli = build_tree_sitter_cli(b);
    if (cli == nullptr) return 1;
    if (strcmp(run->args[0], "corpus") == 0)
        return build_spawn(&(BuildSpawn){.argv = smith_list(b, cli, "test", nullptr), .folder = "tree-sitter"});
    const char *c_source = build_package_path(
        b, &(SmithPackage){.url = "https://github.com/tree-sitter/tree-sitter-c/archive/refs/tags/v0.24.2.tar.gz",
                           .hash = "sha256:2eeb4db31f8fa0865e45488503d13403923bcb485a1bdb637abff8c42dd97364"});
    const char *vendor = "tree-sitter/.vendor/tree-sitter-c";
    int status = build_remove(b, vendor);
    status = status != 0 ? status : build_make_folder(b, vendor);
    status = status != 0 ? status
                         : build_exec(smith_list(b, "/bin/cp", smith_fmt(b, "%s/grammar.js", c_source),
                                                 smith_fmt(b, "%s/", vendor), nullptr));
    return status != 0 ? status
                       : build_spawn(&(BuildSpawn){.argv = smith_list(b, cli, "generate", "--abi", "15", nullptr),
                                                   .folder = "tree-sitter"});
}

// The steps of the parser and the object that embeds it in smith (into *data, which smith links).
static void build_tree_sitter_data(SmithBuild *b, const BuildConfig *config, const char *folder, SmithObject *data) {
    SmithStep generate = {
        .run = build_tree_sitter_run,
        .args = smith_list(b, "generate", nullptr),
        .inputs = smith_list(b, "tree-sitter/grammar.js", "tree-sitter/tree-sitter.json", nullptr),
        .outputs = smith_list(b, "tree-sitter/src/parser.c", "tree-sitter/src/grammar.json",
                              "tree-sitter/src/node-types.json", "tree-sitter/src/tree_sitter/alloc.h",
                              "tree-sitter/src/tree_sitter/array.h", "tree-sitter/src/tree_sitter/parser.h", nullptr),
        .description = "generating the tree-sitter parser",
        .restat = true, // the files are written again with the same bytes: nothing after them reruns
    };
    smith_step(b, &generate);
    SmithObject parser = {
        .compiler = config->cc,
        .source = "tree-sitter/src/parser.c",
        .output = smith_fmt(b, "%s/tree-sitter-parser.o", folder),
        .flags = smith_cat(b, smith_list(b, "-std=gnu11", "-O1", "-w", "-fPIC", "-I", "tree-sitter/src", nullptr),
                           config->platform->deployment, nullptr),
        .implicit_inputs = generate.outputs,
    };
    smith_object(b, &parser);
    // Not smith_program: that links the handlers runtime into every program, which a library Neovim loads must not
    // need.
    const char *library = smith_fmt(b, "%s/smith-tree-sitter.%s", folder, build_is_linux(b) ? "so" : "dylib");
    SmithStep link = {
        .command = smith_cat(b, smith_list(b, config->cc, "-shared", nullptr), config->platform->deployment,
                             smith_list(b, parser.output, "-o", library, nullptr), nullptr),
        .inputs = parser.step.outputs,
        .implicit_inputs = smith_list(b, build_compiler_file(b, config), nullptr),
        .outputs = smith_list(b, library, nullptr),
        .description = smith_fmt(b, "link %s", library),
    };
    smith_step(b, &link);
    *data = (SmithObject){
        .compiler = config->cc,
        .source = "runtime/tree_sitter_data.c",
        .output = smith_fmt(b, "%s/tree_sitter_data.o", folder),
        .flags =
            smith_cat(b, config->cflags,
                      (const char *[]){smith_fmt(b, "-DSMITH_TREE_SITTER_LIBRARY=\"%s\"", library), nullptr}, nullptr),
        .implicit_inputs = link.outputs,
    };
    smith_object(b, data);
    SmithStep task = {
        .run = build_tree_sitter_run,
        .args = smith_list(
            b, smith_option(b, "corpus", "tree-sitter: run the grammar's corpus tests too") ? "corpus" : "none",
            nullptr),
        .inputs = generate.outputs,
        .description = "generating the tree-sitter parser (-Dcorpus: and testing the grammar)",
    };
    smith_task(b, "tree-sitter", &task);
}

// Neovim for the editor tests (#1017): the plugin needs 0.11+ (`vim.uv`) and the hosted Linux runners have 0.9. The
// task `nvim` fetches the pinned release (a .tar.gz, by version and sha256, extracted by smith's own reader) and links
// it at $XDG_CACHE_HOME/smith/tools/nvim-0.11.5 (else ~/.cache/smith/tools), whose bin/ CI puts on the PATH. Linux
// x86_64 only: a Mac has its own Neovim.
static int build_nvim_run(SmithRun *run) {
    SmithBuild *b = run->b;
    if (!build_is_linux(b) || build_is_arm(b)) {
        fprintf(stderr, "smith build nvim: only Linux x86_64 fetches Neovim; use your system's\n");
        return 1;
    }
    const char *cache = smith_env(b, "XDG_CACHE_HOME", smith_fmt(b, "%s/.cache", smith_env(b, "HOME", "")));
    const char *tools = smith_fmt(b, "%s/smith/tools", cache);
    const char *link = smith_fmt(b, "%s/nvim-0.11.5", tools);
    const char *source = build_package_path(
        b, &(SmithPackage){.url = "https://github.com/neovim/neovim/releases/download/v0.11.5/nvim-linux-x86_64.tar.gz",
                           .hash = "sha256:b2f91117be5b5ea39edd7297156dc2a4a8df4add6c95a90809a8df19e7ab6f52"});
    if (build_make_folder(b, tools) != 0) return 1;
    int status = build_remove(b, link);
    status = status != 0 ? status : build_exec(smith_list(b, "/bin/ln", "-s", source, link, nullptr));
    if (status == 0) printf("%s/bin\n", link);
    return status;
}

static void build_nvim_task(SmithBuild *b) {
    SmithStep task = {.run = build_nvim_run, .description = "fetching Neovim 0.11.5 for the editor tests"};
    smith_task(b, "nvim", &task);
}

// ---- The docs gate (#313): AGENTS.md says a change that adds something updates its skill and its concept in the same
// commit; this makes `smith check` say so. The commits on the branch since the base, taken together, must change
// something under .agents/skills/ or knowledge/ when any of them has a subject starting with `feat`. On main itself,
// with no `feat` commit, or when the base cannot be resolved (a shallow clone), it passes, and says so in the last
// case.
static const char *const build_gate_message = "feat commits need their skill and concept updated (AGENTS.md); none of "
                                              ".agents/skills/ or knowledge/ changed since";

// The lines `git -C dir args...` prints, or nullptr when git fails.
static const char *const *build_gate_git(SmithRun *run, const char *dir, const char *const *args) {
    const char *argv[16] = {"git", "-C", dir};
    size_t count = 3;
    for (size_t i = 0; args[i] != nullptr && count < 15; i++)
        argv[count++] = args[i];
    argv[count] = nullptr;
    const char *const *lines = nullptr;
    return smith_run_status(run, argv, &lines) == 0 ? lines : nullptr;
}

// The commits of the branch whose subject starts with `feat`, one line each ("hash subject"). A commit whose patch is
// already on the base (a branch merged before its squash reached main) is dropped by --cherry-pick, so it is not
// asked for docs again. `range` is base...HEAD.
static const char *build_gate_feat_commits(SmithRun *run, const char *dir, const char *range) {
    const char *const *log = build_gate_git(
        run, dir,
        (const char *[]){"log", "--no-merges", "--cherry-pick", "--right-only", "--format=%h %s", range, nullptr});
    const char *found = "";
    for (size_t i = 0; log != nullptr && log[i] != nullptr; i++) {
        const char *subject = strchr(log[i], ' ');
        if (subject != nullptr && strncmp(subject + 1, "feat", 4) == 0)
            found = smith_fmt(run->b, "%s  %s\n", found, log[i]);
    }
    return found;
}

static int build_gate_run(SmithRun *run, const char *dir, const char *base) {
    const char *const *branch =
        build_gate_git(run, dir, (const char *[]){"rev-parse", "--abbrev-ref", "HEAD", nullptr});
    if (branch != nullptr && branch[0] != nullptr && strcmp(branch[0], "main") == 0) return 0;
    const char *const *mergebase = build_gate_git(run, dir, (const char *[]){"merge-base", base, "HEAD", nullptr});
    if (mergebase == nullptr || mergebase[0] == nullptr) {
        printf("docs gate: %s cannot be resolved here (a shallow clone or no remote): not checked\n", base);
        return 0;
    }
    const char *feats = build_gate_feat_commits(run, dir, smith_fmt(run->b, "%s...HEAD", base));
    if (feats[0] == '\0') return 0;
    const char *const *changed =
        build_gate_git(run, dir,
                       (const char *[]){"diff", "--name-only", smith_fmt(run->b, "%s...HEAD", base), "--",
                                        ".agents/skills", "knowledge", nullptr});
    if (changed != nullptr && changed[0] != nullptr) return 0;
    fprintf(stderr, "%s %s\nthe feat commits:\n%s", build_gate_message, base, feats);
    return 1;
}

// args: the base ref, normally origin/main.
static int build_docs_gate(SmithRun *run) {
    return build_gate_run(run, ".", run->args[0]);
}

static int build_gate_fail(const char *message) {
    fputs(message, stderr);
    return 1;
}

// Runs git in a scratch repository for the test of the gate; 0 on success.
static int build_gate_scratch(const char *dir, const char *const *args) {
    const char *argv[16] = {
        "git", "-C", dir, "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false"};
    size_t count = 9;
    for (size_t i = 0; args[i] != nullptr && count < 15; i++)
        argv[count++] = args[i];
    argv[count] = nullptr;
    return build_spawn(&(BuildSpawn){.argv = argv, .log = "/dev/null"});
}

// One commit in the scratch repository: the file `path` written (its folders made) and committed with `subject`.
static int build_gate_commit(const char *dir, const char *path, const char *subject) {
    char full[PATH_MAX];
    snprintf(full, sizeof full, "%s/%s", dir, path);
    for (char *slash = strchr(full + strlen(dir) + 1, '/'); slash != nullptr; slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        mkdir(full, 0755);
        *slash = '/';
    }
    FILE *file = fopen(full, "ab");
    if (file == nullptr) return 1;
    fputs(subject, file);
    fclose(file);
    int status = build_gate_scratch(dir, (const char *[]){"add", "-A", nullptr});
    return status != 0 ? status : build_gate_scratch(dir, (const char *[]){"commit", "-q", "-m", subject, nullptr});
}

// The gate on a scratch repository: a fix passes, a feat that changes only code fails, a skill or concept change
// makes it pass, a base that cannot be resolved passes.
static int build_docs_gate_test(SmithRun *run) {
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s/smith-gate-%d.noindex", getenv("TMPDIR") != nullptr ? getenv("TMPDIR") : "/tmp",
             (int)getpid());
    if (mkdir(dir, 0700) != 0) return 1;
    int status = build_gate_scratch(dir, (const char *[]){"init", "-q", "-b", "main", nullptr});
    status = status != 0 ? status : build_gate_commit(dir, "a.txt", "chore: start");
    status = status != 0 ? status : build_gate_scratch(dir, (const char *[]){"checkout", "-q", "-b", "work", nullptr});
    status = status != 0 ? status : build_gate_commit(dir, "runtime/a.c", "fix: a bug");
    int failed = status;
    if (status == 0 && build_gate_run(run, dir, "main") != 0) failed = build_gate_fail("gate: a fix must pass\n");
    status = failed != 0 ? failed : build_gate_commit(dir, "runtime/b.c", "feat: a feature");
    failed = failed != 0 ? failed : status;
    if (failed == 0) {
        fputs("(the next gate failure is the one under test)\n", stderr);
        if (build_gate_run(run, dir, "main") == 0) failed = build_gate_fail("gate: a feat with no docs must fail\n");
    }
    status = failed != 0 ? failed : build_gate_commit(dir, ".agents/skills/x/SKILL.md", "docs: the skill");
    failed = failed != 0 ? failed : status;
    if (failed == 0 && build_gate_run(run, dir, "main") != 0)
        failed = build_gate_fail("gate: a skill change must pass\n");
    if (failed == 0 && build_gate_run(run, dir, "nowhere") != 0)
        failed = build_gate_fail("gate: an unknown base must pass\n");
    // A branch that merged a feat branch whose squash is already on main, and adds only a test, passes; its own feat
    // commit without docs still fails.
    int s = failed;
    s = s != 0 ? s : build_gate_scratch(dir, (const char *[]){"checkout", "-q", "main", nullptr});
    s = s != 0 ? s : build_gate_scratch(dir, (const char *[]){"checkout", "-q", "-b", "pre", nullptr});
    s = s != 0 ? s : build_gate_commit(dir, "runtime/c.c", "feat: pre-squash");
    s = s != 0 ? s : build_gate_scratch(dir, (const char *[]){"checkout", "-q", "main", nullptr});
    s = s != 0 ? s : build_gate_commit(dir, "runtime/c.c", "feat: pre-squash");
    s = s != 0 ? s : build_gate_scratch(dir, (const char *[]){"checkout", "-q", "-b", "after", "pre", nullptr});
    s = s != 0 ? s : build_gate_commit(dir, "tests/t.smith", "test: only a test");
    failed = failed != 0 ? failed : s;
    if (failed == 0 && build_gate_run(run, dir, "main") != 0)
        failed = build_gate_fail("gate: a feat already squashed on main must pass\n");
    s = failed != 0 ? failed : build_gate_commit(dir, "runtime/d.c", "feat: own");
    failed = failed != 0 ? failed : s;
    if (failed == 0) {
        fputs("(the next gate failure is the one under test)\n", stderr);
        if (build_gate_run(run, dir, "main") == 0)
            failed = build_gate_fail("gate: an own feat with no docs must fail\n");
    }
    build_spawn(&(BuildSpawn){.argv = (const char *[]){"/bin/rm", "-rf", dir, nullptr}});
    return failed;
}

// `smith build ci-times [-Dn=10] [-Devent=pull_request]` (issue #360): where CI's time goes. The last n green runs of
// ci.yml (a pull request, a merge queue entry with -Devent=merge_group, or a push to main) are downloaded with `gh`
// into .smith/ci-times/<run id> (kept, so a run is fetched once): every job's steps in seconds (steps.tsv) and the
// `test-log-*` artifacts, one for each part of the test matrix. scripts/ci_times.c, compiled here, prints the report.
// A run that had no tests (its scope was documents only) has no logs and is counted.
static int build_ci_times_fetch(SmithBuild *b, const char *id, const char *folder) {
    const char *steps = smith_fmt(b, "%s/steps.tsv", folder);
    if (access(steps, R_OK) == 0) return 0;
    if (build_make_folder(b, folder) != 0) return 1;
    const char *jq = ".jobs[] | select(.started_at != null and .completed_at != null) | .name as $n | "
                     "(\"\\($n) / queued\\t\\((.started_at | fromdate) - (.created_at | fromdate))\"), "
                     "(\"\\($n) / job\\t\\((.completed_at | fromdate) - (.started_at | fromdate))\"), "
                     "(.steps[] | select(.started_at != null and .completed_at != null) | "
                     "\"\\($n) / \\(.name)\\t\\((.completed_at | fromdate) - (.started_at | fromdate))\")";
    int status = build_spawn(&(BuildSpawn){
        .argv = smith_list(b, "gh", "api", "--paginate", smith_fmt(b, "repos/{owner}/{repo}/actions/runs/%s/jobs", id),
                           "--jq", jq, nullptr),
        .log = steps,
        .stdout_only = true,
        .tail = 5});
    // A run with no test job, or whose artifacts expired, has no logs: that is counted, not an error.
    (void)build_spawn(&(BuildSpawn){.argv = smith_list(b, "gh", "run", "download", id, "-p", "test-log-*", "-D",
                                                       smith_fmt(b, "%s/", folder), nullptr),
                                    .log = smith_fmt(b, "%s/download.log", folder)});
    return status;
}

static int build_ci_times(SmithRun *run) {
    SmithBuild *b = run->b;
    const char *root = ".smith/ci-times";
    const char *event = run->args[1];
    const char *list = smith_fmt(b, "%s/runs.txt", root);
    if (build_make_folder(b, root) != 0) return 1;
    const char *const *query =
        smith_list(b, "gh", "run", "list", "--workflow", "ci.yml", "--event", event, "--status", "success", "--limit",
                   run->args[0], "--json", "databaseId", "--jq", ".[].databaseId", nullptr);
    if (strcmp(event, "push") == 0) query = smith_cat(b, query, smith_list(b, "--branch", "main", nullptr), nullptr);
    int status = build_spawn(&(BuildSpawn){.argv = query, .log = list, .stdout_only = true, .tail = 5});
    const char *const *ids = build_lines(b, list);
    for (size_t i = 0; status == 0 && ids != nullptr && ids[i] != nullptr; i++)
        status = build_ci_times_fetch(b, ids[i], smith_fmt(b, "%s/%s", root, ids[i]));
    const char *tool = smith_fmt(b, "%s/ci_times", root);
    const char *const compile[] = {
        smith_env(b, "CC", "smith-cc"), "-std=c23", "-O1", "scripts/ci_times.c", "-o", tool, nullptr};
    status = status != 0 ? status : build_exec(compile);
    if (status != 0) return status;
    return build_exec(smith_cat(b, smith_list(b, tool, root, nullptr), ids, nullptr));
}

// `smith build bench [-Dfull=1] [-Dsmith=<path>]` (issue #639): measures each budget line of the speed
// table (no-op build, one-file build, fresh worktree, fmt, lint, a .smith module's compile, optionally the full test
// suite's CPU) with the smith this build made, prints the table and writes it to .smith/bench.tsv.
// scripts/bench.c, compiled here, does the measuring.
static int build_bench(SmithRun *run) {
    SmithBuild *b = run->b;
    const char *folder = ".smith/bench";
    const char *tool = smith_fmt(b, "%s/bench", folder);
    if (build_make_folder(b, ".smith") != 0 || build_make_folder(b, folder) != 0) return 1;
    const char *const compile[] = {
        smith_env(b, "CC", "smith-cc"), "-std=c23", "-O1", "scripts/bench.c", "-o", tool, nullptr};
    int status = build_exec(compile);
    if (status != 0) return status;
    const char *const *argv = smith_list(b, tool, run->args[0], ".smith/bench.tsv", nullptr);
    if (run->args[1][0] != '\0') argv = smith_cat(b, argv, smith_list(b, "full", nullptr), nullptr);
    return build_exec(argv);
}

// `smith build coverage-fetch` (issue #456), run by `smith test --changed` when it finds no records and this build file
// defines the task. CI's ci.yml uploads no coverage records (the check that did is gone, #798), so there is nothing to
// fetch: the task finds none, quietly, and `smith test --changed` falls back to the build graph.
static int build_coverage_fetch(SmithRun *run) {
    (void)run;
    return 0;
}

static void build_developer_tasks(SmithBuild *b, const char *binary) {
    SmithStep friction = {
        .run = build_friction,
        .args =
            smith_list(b, smith_option_value(b, "since", "", "friction: the first merge date, YYYY-MM-DD"), nullptr),
    };
    smith_task(b, "friction", &friction);
    SmithStep coverage_fetch = {.run = build_coverage_fetch};
    smith_task(b, "coverage-fetch", &coverage_fetch);
    SmithStep ci_times = {
        .run = build_ci_times,
        .args = smith_list(
            b, smith_option_value(b, "n", "10", "ci-times: how many of the last green runs"),
            smith_option_value(b, "event", "pull_request", "ci-times: pull_request, merge_group or push (main)"),
            nullptr),
        .description = "reporting where CI's time goes",
    };
    smith_task(b, "ci-times", &ci_times);
    SmithStep bench = {
        .run = build_bench,
        .args = smith_list(b, smith_option_value(b, "smith", binary, "bench: the smith to measure"),
                           smith_option_value(b, "full", "", "bench: 1 also runs the full test suite"), nullptr),
        .description = "measuring the speed budgets of #639",
    };
    smith_task(b, "bench", &bench);
    SmithStep gate = {
        .run = build_docs_gate,
        .args = smith_list(b, "origin/main", nullptr),
        .description = "checking that feat commits update their skill and concept",
        .test = true, // part of `smith check`
    };
    smith_task(b, "check-docs-gate", &gate);
    SmithStep gate_test = {
        .run = build_docs_gate_test,
        .description = "testing the docs gate on a scratch repository",
        .test = true,
    };
    smith_task(b, "test-docs-gate", &gate_test);
}

// Whether the LLVM install has the token hook (patches/clang-token-hook.patch), which `smith build llvm` applies: an
// install made before it has no TokenHook in its headers.
static bool build_has_hook(SmithBuild *b, const char *llvm) {
    const char *script = "grep -q 'setHookMode' \"$0/include/clang/Lex/Preprocessor.h\" && echo yes || echo no";
    return strcmp(smith_capture(b, (const char *[]){"sh", "-c", script, llvm, nullptr}), "yes") == 0;
}

// Whether the LLVM install has the command hook (patches/smith-command-hook.patch), which the shims of clangd and
// clang-tidy install: an install made before r3 has none.
static bool build_has_command_hook(SmithBuild *b, const char *llvm) {
    const char *script = "grep -q 'CLANG_HAS_COMMAND_HOOK' \"$0/include/clang/Driver/CreateInvocationFromArgs.h\" && "
                         "echo yes || echo no";
    return strcmp(smith_capture(b, (const char *[]){"sh", "-c", script, llvm, nullptr}), "yes") == 0;
}

// Whether the LLVM install has what smith links clang, lld and the tools from.
static bool build_llvm_linkable(SmithBuild *b, const char *llvm) {
    return build_has_command_hook(b, llvm) && access(smith_fmt(b, "%s/lib/libclangmain.a", llvm), R_OK) == 0 &&
           access(smith_fmt(b, "%s/lib/%s", llvm, build_platform(b)->lld), R_OK) == 0 &&
           access(smith_fmt(b, "%s/lib/libsmithtools.a", llvm), R_OK) == 0 &&
           access(smith_fmt(b, "%s/lib/smith-clang.id", llvm), R_OK) == 0;
}

// The LLVM install that `smith build llvm` makes, which smith links clang from (smith always compiles in its own
// process). The folder is named even when nothing is built there yet: the default target says so, the others work.
static const char *build_llvm_install(SmithBuild *b) {
    return build_variable(b, "LLVM", smith_fmt(b, "%s/install", build_llvm_folder(b)),
                          "the LLVM install smith links clang from");
}

// Z3 and Alive2 built beside the LLVM install (build_prove_libs_make puts them in ../prove of the install); "" when
// there are none, and the smith has no `smith prove`.
static const char *build_prove_folder(SmithBuild *b, const char *llvm) {
    const char *folder = smith_fmt(b, "%s/../prove", llvm);
    bool linkable = access(smith_fmt(b, "%s/lib/libz3.a", folder), R_OK) == 0 &&
                    access(smith_fmt(b, "%s/lib/libbitwuzla.a", folder), R_OK) == 0 &&
                    access(smith_fmt(b, "%s/lib/libalive2_ir.a", folder), R_OK) == 0;
    return linkable ? folder : "";
}

// Enzyme's static library, built beside the LLVM install (build_enzyme_make); "" when there is none, and the smith
// cannot differentiate.
static const char *build_enzyme_install(SmithBuild *b) {
    const char *folder = build_enzyme_folder(b);
    return access(smith_fmt(b, "%s/lib/libEnzymeStatic-23.a", folder), R_OK) == 0 ? folder : "";
}

void build(SmithBuild *b) {
    const char *std = build_variable(b, "STD", "gnu23", "the C standard");
    const char *werror = build_variable(b, "WERROR", "-Werror", "-Werror, or empty to let warnings pass");
    const char *folder = build_variable(b, "BUILD", smith_fmt(b, "build/%s-%s", b->os, b->arch), "the build folder");
    BuildConfig config = {
        .platform = build_platform(b),
        .cc = build_variable(b, "CC", "smith-cc", "the smith that compiles smith: it compiles the .smith libraries"),
        .prefix = build_variable(b, "PREFIX", smith_fmt(b, "%s/.local", smith_env(b, "HOME", "")), "install here"),
        .werror = werror,
        .llvm = build_llvm_install(b),
    };
    config.linkable = build_llvm_linkable(b, config.llvm);
    config.llvm_id =
        smith_capture_or(b, (const char *[]){"cat", smith_fmt(b, "%s/lib/smith-clang.id", config.llvm), nullptr}, "");
    // Not the environment's CXX, which may be smith-c++ itself: the shim wants LLVM's own headers and a clang.
    config.cxx = smith_option_value(b, "CXX", smith_fmt(b, "%s/bin/clang++", config.llvm), "compiles shim/clang.cpp");
    config.prove = build_prove_folder(b, config.llvm);
    config.hook = build_has_hook(b, config.llvm);
    config.enzyme = build_enzyme_install(b);
    config.coverage =
        build_variable(b, "COVERAGE", "", "COVERAGE=1: count the lines smith runs (the check that records)")[0] != '\0';
    config.lldb = access(smith_fmt(b, "%s/lib/libsmithlldb.a", config.llvm), R_OK) == 0;
    config.cflags = smith_cat(
        b, (const char *[]){smith_fmt(b, "-std=%s", std), "-O1", "-g", "-Wall", "-Wextra", "-Wpedantic", nullptr},
        smith_when(b, werror[0] != '\0', (const char *[]){werror, nullptr}), config.platform->deployment, nullptr);

    config.headers = build_header_steps(b, &config);
    config.protocol = build_protocol_steps(b, &config, folder);
    SmithStep smith = build_programs(b, &config, folder);
    build_other_objects(b, &config, folder,
                        build_template_modules(b, &config, folder,
                                               smith_cat(b, build_test_modules(b, &config, folder), config.headers,
                                                         config.protocol, nullptr)));
    build_unbuilt_sources(b, &config);
    config.binary = smith.outputs[0];
    const char *const *tested = build_test(b, &config, config.binary, folder);
    build_install(b, &config, config.binary, tested);
    build_release(b, config.binary);
    build_nvim_task(b);
    build_llvm(b);
    build_developer_tasks(b, config.binary);
    SmithStep clean = {.command = (const char *[]){"rm", "-rf", "build", nullptr}};
    smith_task(b, "clean", &clean);
    // clangd reads this: the exact command of each compile, and build.c's own.
    smith_compile_commands(b, "compile_commands.json");
}
