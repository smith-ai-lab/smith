// The build of smith, run with `smith build`. It replaces the Makefile and runs the commands the Makefile ran.
//
// smith is built by its previous release (knowledge/smith-bootstrap.md): smith.c is C23 and its libraries are .smith
// modules, compiled by the smith that runs this build (`smith-cc`, the default CC), so a smith with the token hook and
// the LLVM of `smith build llvm` is needed. A fresh clone with no smith runs scripts/stage0.sh, which downloads the
// newest release of an ancestor commit and runs its `smith build`.
//
// The verbs (smith test, smith check, smith fmt, smith lint, smith docs, smith prove, smith run) mean the same here as
// in every project and read what this file declares: the tests below are the tests, the compiles are what lint and
// prove look at (sources that are never built but should be linted are declared with smith_compile_command). Targets of
// smith build: the default, text-exhaustive, headers, test-compare -DREAL=..., install (-DCC=...,
// -DPREFIX=...), release (CI only), llvm and clean.
#include <smith/build.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Everything the targets share.
typedef struct {
    const char *cc;
    const char *const *cflags;
    const char *binary; // the smith this build makes
    const char *real;   // the compiler `compare` checks smith against
    const char *prefix;
    const char *werror;
    const char *llvm;    // the LLVM install smith links clang from, so that it compiles in its own process; else ""
    const char *llvm_id; // the identity of that clang (scripts/llvm.sh): its version and a hash of what is linked
    const char *cxx;     // compiles the shims
    bool lldb;           // the install has lldb (scripts/llvm.sh, the lldb step): smith has `smith dbg` and `smith dap`
    const char *prove;   // the folder with Z3 and Alive2 beside the LLVM install (scripts/prove-libs.sh), else ""
    bool linkable;       // the install has what smith links (else the default target stops and says `smith build llvm`)
    bool hook;           // the install has the token hook (patches/clang-token-hook.patch): smith compiles .smith files
    const char
        *enzyme; // the folder with libEnzymeStatic-23.a (scripts/enzyme.sh): smith differentiates at link time, else ""
} BuildConfig;

// A make variable set with ?=: -DNAME=value, else the environment, else the fallback.
static const char *build_variable(SmithBuild *b, const char *name, const char *fallback, const char *description) {
    return smith_option_value(b, name, smith_env(b, name, fallback), description);
}

// The flags of a .smith source: the build's C flags, the include folder of the generated headers, and C2y (as `smith
// cc` compiles a module, so `_Countof` works).
static const char *const *build_smith_flags(SmithBuild *b, const BuildConfig *config) {
    return smith_cat(b, config->cflags, (const char *[]){"-I", "include", "-std=gnu2y", nullptr}, nullptr);
}

// The protocol code smith's debugger server stands on, generated from the official schemas (scripts/protocol.c,
// knowledge/protocol-generation.md): DAP's and MCP's, each a package pinned by hash. The generated files are committed,
// so a build never needs the network; `smith build protocol` writes them again from the packages, and `smith check`
// runs check-protocol (a test), which fails when a committed file is not what the generator writes now.
typedef struct {
    const char *name;    // dap or mcp: the prefix of the generated names and files
    const char *url;     // the package: the project's release, a tag
    const char *hash;    // `smith fetch <url>` prints this line
    const char *schema;  // the schema's path in the package
    const char *license; // and the license's
    const char *source;  // for the header of every generated file
} BuildProtocol;

static const BuildProtocol build_protocols[] = {
    {"dap", "https://github.com/microsoft/debug-adapter-protocol/archive/refs/tags/v1.71.0.tar.gz",
     "sha256:2b314bf3f3baec7a86401b8e25c2cedf6d6b9faaf1f1ec42ce844628ea7d1f9d", "debugAdapterProtocol.json",
     "License-code.txt", "debugAdapterProtocol.json of microsoft/debug-adapter-protocol v1.71.0"},
    {"mcp", "https://github.com/modelcontextprotocol/modelcontextprotocol/archive/refs/tags/2026-07-28.tar.gz",
     "sha256:7df4915b4a5cdc999876f5a80b7af9912bdea21490827224e8c7a10f2c81470a", "schema/2026-07-28/schema.json",
     "LICENSE", "schema/2026-07-28/schema.json of modelcontextprotocol/modelcontextprotocol, release 2026-07-28"},
};
constexpr size_t build_protocol_count = sizeof build_protocols / sizeof build_protocols[0];

// Whether the package is in the cache already, which is all -DOFFLINE allows.
static bool build_protocol_cached(SmithBuild *b, const BuildProtocol *protocol) {
    const char *cache = smith_env(b, "XDG_CACHE_HOME", smith_fmt(b, "%s/.cache", smith_env(b, "HOME", "")));
    return access(smith_fmt(b, "%s/smith/packages/%s", cache, protocol->hash + strlen("sha256:")), R_OK) == 0;
}

// args: "write" or "check", and "offline" or "online". The package is fetched once per machine (the cache keeps it);
// offline, a package that is not there is skipped and said so, since the committed files cannot be checked without it.
static int build_protocol_run(SmithRun *run) {
    bool check = strcmp(run->args[0], "check") == 0;
    bool offline = strcmp(run->args[1], "offline") == 0;
    for (size_t i = 0; i < build_protocol_count; i++) { // bounded by the table
        const BuildProtocol *protocol = &build_protocols[i];
        if (offline && !build_protocol_cached(run->b, protocol)) {
            printf("protocol: %s is not in the package cache and -DOFFLINE forbids fetching it: not %s\n",
                   protocol->name, check ? "checked" : "written");
            continue;
        }
        const char *folder = smith_package(run->b, &(SmithPackage){.url = protocol->url, .hash = protocol->hash});
        const char *const argv[] = {run->inputs[0],
                                    protocol->name,
                                    smith_fmt(run->b, "%s/%s", folder, protocol->schema),
                                    smith_fmt(run->b, "%s/%s", folder, protocol->license),
                                    protocol->source,
                                    "runtime/protocol",
                                    "tests/protocol",
                                    check ? "--check" : nullptr,
                                    nullptr};
        int status = smith_run_program(run, argv, nullptr);
        if (status != 0) return status;
    }
    return 0;
}

// The generator is a program of smith's own C (scripts/protocol.c, on lib/json.c), built here and not shipped: like
// scripts/pack.c, a tool of the build. arena, contract and text are the modules of smith, compiled from their sources.
static void build_protocol(SmithBuild *b, const BuildConfig *config, const char *folder) {
    const char *const sources[] = {
        "scripts/protocol.c", "lib/smith/arena.smith", "lib/smith/contract.smith", "lib/smith/text.smith", "lib/json.c",
        "lib/trace.c"};
    SmithObject objects[6] = {};
    const SmithObject *linked[7] = {};
    for (size_t i = 0; i < 6; i++) {
        objects[i] = (SmithObject){
            .compiler = config->cc,
            .source = sources[i],
            .output = smith_fmt(b, "%s/protocol-%zu.o", folder, i),
            .flags = smith_cat(b, config->cflags, (const char *[]){"-I", "include", nullptr}, nullptr),
        };
        if (strstr(sources[i], ".smith") != nullptr) objects[i].flags = build_smith_flags(b, config);
        smith_object(b, &objects[i]);
        linked[i] = &objects[i];
    }
    SmithProgram program = {.linker = config->cc, .output = smith_fmt(b, "%s/protocol", folder), .objects = linked};
    smith_program(b, &program);
    bool offline = smith_option(b, "OFFLINE", "a package missing from the cache is an error instead of a fetch");
    SmithStep write = {
        .run = build_protocol_run,
        .description = "writing the protocol code from the pinned schemas",
        .inputs = program.step.outputs,
        .args = smith_list(b, "write", offline ? "offline" : "online", nullptr),
    };
    smith_task(b, "protocol", &write);
    SmithStep check = {
        .run = build_protocol_run,
        .description = "checking the protocol code against the pinned schemas",
        .inputs = program.step.outputs,
        .args = smith_list(b, "check", offline ? "offline" : "online", nullptr),
        .test = true,
    };
    smith_task(b, "check-protocol", &check);
}

// The libraries written in .smith (lib/smith/<name>.smith, the module smith::<name>): their C headers,
// include/smith/<name>.h, are generated and committed, because the stage0 has no hook and embeds them as bytes. Porting
// a library adds its name here. `smith build headers` regenerates them; `check-headers` (a test, so part of `smith
// check`) fails when a committed header differs from what its module generates. The header is written as the generator
// makes it and formatted with the repository's .clang-format (smith fmt keeps it so; the stage0 formats with Homebrew's
// clang-format, the same version), so it reads like one written by hand.
static const char *const build_generated_headers[] = {"arena", "autodiff", "build",  "contract", "format", "inflate",
                                                      "io",    "math",     "random", "test",     "text",   nullptr};

// Generates each module's header into a scratch folder and runs `action` (sh, with $name and $dir) on it.
static SmithStep build_header_step(SmithBuild *b, const BuildConfig *config, const char *action) {
    const char *names = smith_join(b, build_generated_headers, " ");
    return (SmithStep){
        .command = smith_list(b, "sh", "-c",
                              smith_fmt(b,
                                        "dir=$(mktemp -d) && trap 'rm -rf \"$dir\"' EXIT && status=0 && "
                                        "for name in %s; do "
                                        "\"$0\" cc --smith-header lib/smith/$name.smith -o \"$dir/$name.h\" || exit 1; "
                                        "\"$0\" fmt --style=file:.clang-format -i \"$dir/$name.h\" || exit 1; "
                                        "%s; done; exit $status",
                                        names, action),
                              config->binary, nullptr),
        .inputs = smith_list(b, config->binary, nullptr),
    };
}

static void build_headers(SmithBuild *b, const BuildConfig *config) {
    SmithStep rewrite = build_header_step(b, config, "cp \"$dir/$name.h\" include/smith/$name.h");
    SmithStep check = build_header_step(b, config,
                                        "cmp -s \"$dir/$name.h\" include/smith/$name.h || { echo "
                                        "\"include/smith/$name.h is not what lib/smith/$name.smith generates; run "
                                        "smith build headers\"; status=1; }");
    check.test = true; // part of `smith test`, so of `smith check`
    smith_task(b, "headers", &rewrite);
    smith_task(b, "check-headers", &check);
}

// The libraries of the LLVM install that the in-process clang and the tools link: libsmithtools.a first, in whole,
// which holds the mains of clang-format, clang-tidy and clangd and what they need patched (scripts/llvm.sh), and so is
// found before libclangTidyMain.a's, then all of clang's and of LLVM's, clangd's other libraries (libclangd[A-Z]*
// matches none of them) and libclangmain.a, which holds clang's main. The linker's -dead_strip drops what the driver,
// the compiler and the targets do not reach, so the list is not worked out by hand.
static const char *const *build_clang_libraries(SmithBuild *b, const char *llvm) {
    const char *const *found = smith_glob(b, (const char *[]){smith_fmt(b, "%s/lib/libclang[A-Z]*.a", llvm),
                                                              smith_fmt(b, "%s/lib/libLLVM*.a", llvm), nullptr});
    const char *const *first = smith_list(b, smith_fmt(b, "-Wl,-force_load,%s/lib/libsmithtools.a", llvm), nullptr);
    const char *const *more =
        smith_list(b, smith_fmt(b, "%s/lib/libclangdSupport.a", llvm),
                   smith_fmt(b, "%s/lib/libclangdRemoteIndex.a", llvm), smith_fmt(b, "%s/lib/libclangmain.a", llvm),
                   smith_fmt(b, "%s/lib/liblldMachO.a", llvm), smith_fmt(b, "%s/lib/liblldCommon.a", llvm), nullptr);
    return smith_cat(b, first, found, more, nullptr);
}

// lldb's libraries, when the install has them (scripts/llvm.sh copies the static ones of its build into the install):
// the plugins and the core, libsmithlldb.a with the SB API and the two mains, and what lldb's host layer uses.
static const char *const *build_lldb_libraries(SmithBuild *b, const BuildConfig *config) {
    if (!config->lldb) return smith_list(b, nullptr);
    const char *const *found =
        smith_glob(b, (const char *[]){smith_fmt(b, "%s/lib/liblldb[A-Z]*.a", config->llvm), nullptr});
    return smith_cat(b, found,
                     (const char *[]){smith_fmt(b, "%s/lib/libsmithlldb.a", config->llvm), "-ledit", "-framework",
                                      "Foundation", "-framework", "CoreFoundation", "-framework", "CoreServices",
                                      "-framework", "Security", nullptr},
                     nullptr);
}

// Enzyme (scripts/enzyme.sh), before LLVM's libraries: shim/lld.cpp registers it with the linker's LTO pipeline.
static const char *const *build_enzyme_libraries(SmithBuild *b, const BuildConfig *config) {
    if (config->enzyme[0] == '\0') return smith_list(b, nullptr);
    return smith_list(b, smith_fmt(b, "%s/lib/libEnzymeStatic-23.a", config->enzyme), nullptr);
}

// shim/clang.cpp, which gives smith.c clang's main. The LLVM headers are system headers, so the build's warnings stay
// smith's own.
static void build_shim(SmithBuild *b, const BuildConfig *config, const char *folder, const char *source,
                       const char *name, SmithObject *shim) {
    *shim = (SmithObject){
        .compiler = config->cxx,
        .source = source,
        .output = smith_fmt(b, "%s/%s.o", folder, name),
        .flags = smith_cat(
            b,
            (const char *[]){"-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-fno-exceptions", "-fno-rtti",
                             "-D__STDC_CONSTANT_MACROS", "-D__STDC_FORMAT_MACROS", "-D__STDC_LIMIT_MACROS", "-isystem",
                             smith_fmt(b, "%s/include", config->llvm), "-isystem", "shim/lldb", nullptr},
            smith_when(b, config->werror[0] != '\0', (const char *[]){config->werror, nullptr}),
            smith_when(b, config->hook, (const char *[]){"-DSMITH_HOOK", nullptr}),
            smith_when(b, config->enzyme[0] != '\0', (const char *[]){"-DSMITH_ENZYME", nullptr}), nullptr),
    };
    smith_object(b, shim);
}

// The prover (`smith prove`): shim/prove*.cpp, which need RTTI and exceptions for Alive2, and the libraries they link
// before LLVM's: Alive2's (scripts/prove-libs.sh builds them beside the LLVM install, which has RTTI and exceptions on)
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
                  smith_when(b, config->werror[0] != '\0', (const char *[]){config->werror, nullptr}), nullptr);
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
    // GMP and MPFR are Bitwuzla's: built static beside the rest (scripts/bitwuzla.sh).
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
        .libraries = (const char *[]){"-lcompression", nullptr},
    };
    smith_program(b, &program);
    const char *pack = smith_fmt(b, "%s/clang-resource.pack", folder);
    SmithStep step = {
        .command =
            (const char *[]){
                "sh", "-c",
                // NOLINTNEXTLINE(bugprone-suspicious-missing-comma): one script, in two literals
                "root=$PWD && cd \"$0\" && (find lib/clang/*/include lib/clang/*/share -type f; "
                "ls lib/clang/*/lib/darwin/*osx* lib/libLTO.dylib) | LC_ALL=C sort | \"$root/$1\" \"$root/$2\"",
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

// Everything smith links beside smith.c when clang is inside: the packed data, the shims of the compiler, the linker
// and the tools, and the prover when its libraries are there. `list` gets pointers to them, ended by nullptr.
typedef struct {
    SmithObject objects[14];
    BuildProver prove;
} BuildShims;

static void build_shims(SmithBuild *b, const BuildConfig *config, const char *folder, const char *pack,
                        BuildShims *shims, const SmithObject **list) {
    build_pack_data(b, config, folder, pack, &shims->objects[0]);
    const char *const sources[] = {"clang", "lld",  "format",   "tidy",         "clangd",      "symbolize", "hook",
                                   "lsp",   "lldb", "lldb_dap", "lldb_explain", "lldb_slices", "affine"};
    size_t count = 0;
    for (size_t i = 0; i < sizeof sources / sizeof sources[0]; i++) {
        bool is_hook =
            strcmp(sources[i], "hook") == 0 || strcmp(sources[i], "lsp") == 0 || strcmp(sources[i], "affine") == 0;
        bool is_lldb = strncmp(sources[i], "lldb", 4) == 0;
        if ((is_hook && !config->hook) || (is_lldb && !config->lldb)) continue;
        build_shim(b, config, folder, smith_fmt(b, "shim/%s.cpp", sources[i]), smith_fmt(b, "%s_shim", sources[i]),
                   &shims->objects[count + 1]);
        count += 1;
    }
    size_t listed = 0;
    for (size_t i = 0; i < count + 1; i++) // the packed data, then the shims
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
    "include/smith/build.h",
    "lib/smith/build.smith",
    "runtime/build_runtime.c",
    "runtime/package.c",
    "runtime/gzip.c",
    "runtime/xz.c",
    "runtime/untar.c",
    "runtime/cache.c",
    "runtime/symbolize.c",
    "runtime/explain.c",
    "shim/lldb_explain.h",
    "runtime/mcp.c",
    "runtime/mcp_text.c",
    "runtime/mcp_dap.c",
    "runtime/mcp_show.c",
    "runtime/mcp_tools.c",
    "runtime/mcp_serve.c",
    "runtime/protocol/protocol.h",
    "runtime/protocol/protocol.c",
    "runtime/protocol/dap.h",
    "runtime/protocol/dap.c",
    "runtime/protocol/mcp.h",
    "runtime/protocol/mcp.c",
    "include/smith/arena.h",
    "lib/smith/arena.smith",
    "include/smith/autodiff.h",
    "lib/smith/autodiff.smith",
    "include/smith/inflate.h",
    "lib/smith/inflate.smith",
    "include/smith/io.h",
    "lib/smith/io.smith",
    "include/smith/random.h",
    "lib/smith/random.smith",
    "include/smith/contract.h",
    "lib/smith/contract.smith",
    "include/smith/format.h",
    "lib/smith/format.smith",
    "include/smith/math.h",
    "lib/smith/math.smith",
    "include/smith/text.h",
    "lib/smith/text.smith",
    "include/smith/json.h",
    "lib/json.c",
    "include/smith/http.h",
    "lib/http.c",
    "include/smith/test.h",
    "lib/smith/test.smith",
    "templates/new/main.c",
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
    "runtime/lang_sparse.c",
    "runtime/lang_declare.c",
    "runtime/lang_tuple.c",
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
static const char *const build_linked_libraries[] = {"arena", "contract", "inflate", "io", "random", "text"};

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

static SmithStep build_programs(SmithBuild *b, const BuildConfig *config, const char *folder) {
    const char *pack = build_resource_pack(b, config, folder);
    const char *const *flags = smith_cat(
        b, config->cflags, (const char *[]){"-I", "include", "-DSMITH_COMMIT_LINKED", "-DSMITH_IN_PROCESS", nullptr},
        (const char *[]){smith_fmt(b, "-DSMITH_CLANG_ID=\"%s\"", config->llvm_id), "-DSMITH_MCP", nullptr},
        smith_when(b, config->prove[0] != '\0', (const char *[]){"-DSMITH_PROVE", nullptr}),
        smith_when(b, config->lldb, (const char *[]){"-DSMITH_LLDB", nullptr}),
        smith_when(b, config->hook, (const char *[]){"-DSMITH_HOOK", nullptr}),
        smith_when(b, config->enzyme[0] != '\0', (const char *[]){"-DSMITH_ENZYME", nullptr}), nullptr);
    // The libraries have smith.c's flags: `smith prove` proves sources whose compile flags are alike together, so a
    // call in smith.c into a library finds the library's body.
    SmithObject object = {
        .compiler = config->cc,
        .source = "smith.c",
        .output = smith_fmt(b, "%s/smith.o", folder),
        .flags = flags,
        .implicit_inputs = smith_cat(b, (const char *[]){"scripts/pack.h", nullptr}, build_smith_inputs,
                                     build_llvm_required(b, config), nullptr),
    };
    smith_object(b, &object);
    SmithObject commit = build_commit_object(b, config, folder);
    BuildShims shims = {};
    SmithObject libraries[sizeof build_linked_libraries / sizeof build_linked_libraries[0]] = {};
    const SmithObject *objects[32] = {&object, &commit};
    size_t count = 2;
    for (size_t i = 0; i < sizeof libraries / sizeof libraries[0]; i++) {
        libraries[i] = (SmithObject){
            .compiler = config->cc,
            .source = smith_fmt(b, "lib/smith/%s.smith", build_linked_libraries[i]),
            .output = smith_fmt(b, "%s/%s.o", folder, build_linked_libraries[i]),
            .flags = flags,
        };
        smith_object(b, &libraries[i]);
        objects[count++] = &libraries[i];
    }
    build_shims(b, config, folder, pack, &shims, objects + count);
    SmithProgram program = {
        .linker = config->cc,
        .output = smith_fmt(b, "%s/smith", folder),
        .flags = smith_cat(b, (const char *[]){"-Wl,-dead_strip", "-Wl,-exported_symbol,_main", nullptr},
                           smith_when(b, config->enzyme[0] != '\0',
                                      (const char *[]){"-Wl,-exported_symbol,_llvmGetPassPluginInfo", nullptr}),
                           nullptr),
        .objects = objects,
        .libraries = smith_cat(b, shims.prove.libraries != nullptr ? shims.prove.libraries : smith_list(b, nullptr),
                               build_enzyme_libraries(b, config), build_clang_libraries(b, config->llvm),
                               build_lldb_libraries(b, config),
                               (const char *[]){"-lc++", "-lm", "-lcompression", nullptr}, nullptr),
    };
    smith_program(b, &program);
    smith_default(b, build_links(b, &program.step, folder));
    return program.step;
}

// The modules of tests/*.smith, for lint: each one's header (.smith/include/<name>.h, which the tests that import
// `scratch` read) and an object nobody asks for, both made by the smith under test (the one this build links: a test
// uses a feature at once). They are plain steps that wait for the smith-cc link, so a fresh build folder's first build
// makes them as its second does. smith_object would put each module's header in the check that every compile of a
// module waits for, and the libraries' compiles, which that link waits for, would then wait for these headers: a
// cycle. Returns what lint needs built (smith_needs reads the inputs of a source's compile): the headers and smith-cc.
static const char *const *build_test_modules(SmithBuild *b, const BuildConfig *config, const char *folder) {
    const char *smith_cc = smith_fmt(b, "%s/smith-cc", folder);
    const char *const *flags = build_smith_flags(b, config);
    const char *const *tests = smith_glob(b, smith_list(b, "tests/*.smith", nullptr));
    const char *const *needs = smith_list(b, smith_cc, nullptr);
    for (size_t i = 0; tests[i] != nullptr; i++) {
        const char *name = smith_replace_extension(b, tests[i] + strlen("tests/"), "");
        const char *header = smith_fmt(b, ".smith/include/%s.h", name);
        const char *object = smith_fmt(b, "%s/lint/%zu.o", folder, i);
        SmithStep write = {
            .command = smith_cat(b, smith_list(b, smith_cc, "--smith-header", tests[i], "-o", header, nullptr), flags,
                                 nullptr),
            .inputs = smith_list(b, tests[i], nullptr),
            .implicit_inputs = smith_list(b, smith_cc, nullptr),
            .outputs = smith_list(b, header, nullptr),
            .description = smith_fmt(b, "header %s", tests[i]),
            .restat = true, // written only when its bytes change
        };
        smith_step(b, &write);
        SmithStep compile = {
            .command = smith_cat(b, smith_list(b, smith_cc, nullptr), flags,
                                 smith_list(b, "-I", ".smith/include", "-c", tests[i], "-o", object, nullptr), nullptr),
            .inputs = smith_list(b, tests[i], nullptr),
            .implicit_inputs = smith_list(b, smith_cc, header, nullptr),
            .outputs = smith_list(b, object, nullptr),
            .description = smith_fmt(b, "compile %s", tests[i]),
        };
        smith_step(b, &compile);
        smith_compile_command(b, smith_cc, tests[i],
                              smith_cat(b, flags, smith_list(b, "-I", ".smith/include", nullptr), nullptr));
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
        .flags = smith_cat(b, config->cflags, (const char *[]){"-I", "include", nullptr}, nullptr),
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
static void build_unbuilt_sources(SmithBuild *b, const BuildConfig *config, const char *folder) {
    const char *const *flags = smith_cat(b, config->cflags, smith_list(b, "-I", "include", nullptr), nullptr);
    const char *const *sources = smith_glob(b, smith_list(b, "lib/*.c", "tests/*.c", "scripts/defer_init.c", nullptr));
    for (size_t i = 0; sources[i] != nullptr; i++) {
        // stub.c is built (build_other_objects) with its -D, tree_sitter.c is the grammar's, and random.c includes the
        // raw lowering of random.smith, which tests/random.sh writes when it runs
        if (strcmp(sources[i], "tests/stub.c") == 0 || strcmp(sources[i], "tests/tree_sitter.c") == 0 ||
            strcmp(sources[i], "tests/random.c") == 0)
            continue;
        smith_compile_command(b, config->cc, sources[i], flags);
    }
    // The modules are linted as written, and `.smith` is C2y (as `smith cc` compiles it), so `_Countof` works.
    const char *const *smith_flags = smith_cat(b, flags, smith_list(b, "-std=gnu2y", nullptr), nullptr);
    smith_compile_command(b, config->cc, "lib/smith/arena.smith", smith_flags);
    smith_compile_command(b, config->cc, "lib/smith/autodiff.smith", smith_flags);
    smith_compile_command(b, config->cc, "lib/smith/contract.smith", smith_flags);
    smith_compile_command(b, config->cc, "lib/smith/format.smith", smith_flags);
    smith_compile_command(b, config->cc, "lib/smith/inflate.smith", smith_flags);
    smith_compile_command(b, config->cc, "lib/smith/io.smith", smith_flags);
    smith_compile_command(b, config->cc, "lib/smith/math.smith", smith_flags);
    smith_compile_command(b, config->cc, "lib/smith/random.smith", smith_flags);
    smith_compile_command(b, config->cc, "lib/smith/test.smith", smith_flags);
    smith_compile_command(b, config->cc, "lib/smith/text.smith", smith_flags);
    smith_compile_command(b, config->cc, "lib/smith/build.smith", smith_flags);
    // The protocol code's hand-written base and its tests' support; the generated files are not linted.
    smith_compile_command(b, config->cc, "runtime/protocol/protocol.c", flags);
    smith_compile_command(b, config->cc, "tests/protocol/protocol_support.c",
                          smith_cat(b, flags, smith_list(b, "-I", "runtime/protocol", nullptr), nullptr));
}

// One task per test script of tests/, `test-<task>`, each one of smith's tests, so `smith test` runs them side by
// side. The scripts are independent: each works in a folder of its own (mktemp) and with caches of its own
// (XDG_CACHE_HOME, and XDG_DATA_HOME where it starts compilers), and the one folder they may share, where a smith
// writes the clang files and its programs on first use, is written by rename. `run` is the routing tests with stub
// compilers (no clang or Xcode needed); the others use real ninja, clang and git. A script that takes long is several
// rows, one per shard (tests/test.sh takes "isolate" and a list of its parts). Add a script by adding a row, in order.
// `prove` starts with the others: a proof's budget is CPU time (counted as on 8 idle cores), so what else runs changes
// how long it takes, never whether it passes.
typedef struct {
    const char *task;
    const char *script;
    const char *mode;  // the script's second argument, or nullptr
    const char *shard; // its third (a list of parts of the script), or nullptr
    int phase;         // 0 is a short task, 1 a long one (all start at once)
} BuildTestScript;

// clang-format off
static const BuildTestScript build_test_scripts[] = {
    {"affine", "affine", nullptr, nullptr, 0},
    {"arena", "arena", nullptr, nullptr, 0},
    {"autodiff", "autodiff", nullptr, nullptr, 1},
    {"build-0", "build", "0/4", nullptr, 1},
    {"build-1", "build", "1/4", nullptr, 1},
    {"build-2", "build", "2/4", nullptr, 1},
    {"build-3", "build", "3/4", nullptr, 1},
    {"build-smith", "build-smith", nullptr, nullptr, 1},
    {"cache", "cache", nullptr, nullptr, 0},
    {"ci_tree", "ci_tree", nullptr, nullptr, 0},
    {"contract", "contract", nullptr, nullptr, 0},
    {"conventions", "conventions", nullptr, nullptr, 1},
    {"dap", "dap", nullptr, nullptr, 0},
    {"defaults", "defaults", nullptr, nullptr, 0},
    {"defer", "defer", nullptr, nullptr, 0},
    {"docs", "docs", nullptr, nullptr, 0},
    {"effects", "effects", nullptr, nullptr, 0},
    {"headers", "headers", nullptr, nullptr, 0},
    {"http", "http", nullptr, nullptr, 0},
    {"inflate", "inflate", nullptr, nullptr, 0},
    {"io", "io", nullptr, nullptr, 0},
    {"json", "json", nullptr, nullptr, 0},
    {"lint-smith", "lint-smith", nullptr, nullptr, 0},
    {"lsp-folders", "lsp", "folders", nullptr, 1},
    {"lsp-project", "lsp", "project", nullptr, 1},
    {"mcp-fake-asan", "mcp", "fake-asan", nullptr, 0},
    {"mcp-fake-fast", "mcp", "fake-fast", nullptr, 0},
    {"mcp-fake-ndebug", "mcp", "fake-ndebug", nullptr, 0},
    {"mcp-real", "mcp", "real", nullptr, 1},
    {"neovim", "neovim", nullptr, nullptr, 0},
    {"package", "package", nullptr, nullptr, 0},
    {"protocol", "protocol", nullptr, nullptr, 0},
    {"prove", "prove", nullptr, nullptr, 1},
    {"prove-sound", "prove_sound", nullptr, nullptr, 1},
    {"random", "random", nullptr, nullptr, 0},
    {"readme", "readme", nullptr, nullptr, 0},
    {"release", "release", nullptr, nullptr, 0},
    {"resource", "resource", nullptr, nullptr, 0},
    {"run", "run", nullptr, nullptr, 0},
    {"skills", "skills", nullptr, nullptr, 0},
    {"smith-fmt", "smith-fmt", nullptr, nullptr, 0},
    {"smith-lang-0", "smith-lang", "0/3", nullptr, 1},
    {"smith-lang-1", "smith-lang", "1/3", nullptr, 1},
    {"smith-lang-2", "smith-lang", "2/3", nullptr, 1},
    {"smith-lang-c", "smith-lang-c", nullptr, nullptr, 0},
    {"stage0", "stage0", nullptr, nullptr, 0},
    {"main", "test", "main", nullptr, 1},
    {"reports", "test", "reports,misc,explain,lang", nullptr, 1},
    {"rest", "test", "alongside,workers,flags,budget,shrink_int,shrink_list,shrink_normal,inprocess", nullptr, 1},
    {"isolate-main", "test", "isolate", "main", 1},
    {"isolate-reports", "test", "isolate", "misc,reports,lang", 1},
    {"isolate-rest", "test", "isolate", "workers,flags,inprocess,explain", 1},
    {"isolate-shrink-int", "test", "isolate", "shrink_int,alongside,budget", 1},
    {"isolate-shrink-list", "test", "isolate", "shrink_list", 1},
    {"isolate-shrink-normal", "test", "isolate", "shrink_normal", 1},
    {"text", "text", nullptr, nullptr, 0},
    {"tree_sitter", "tree_sitter", nullptr, nullptr, 0},
    {"untar", "untar", nullptr, nullptr, 1},
};
// clang-format on

constexpr size_t build_test_script_count = sizeof build_test_scripts / sizeof build_test_scripts[0];

// Stops the build with a message.
static void build_stop(SmithBuild *b, const char *message) {
    (void)smith_capture(b, (const char *[]){"sh", "-c", "echo \"build.c: $0\" >&2; exit 1", message, nullptr});
}

// Whether the list, its names split by `separator`, holds the name.
static bool build_list_has(SmithBuild *b, const char *list, char separator, const char *name) {
    return strstr(smith_fmt(b, "%c%s%c", separator, list, separator),
                  smith_fmt(b, "%c%s%c", separator, name, separator)) != nullptr;
}

// The parts of tests/test.sh and the shards of build_test_scripts must be the same set: a part in no `isolate` row
// would never run under --isolate, and a name in a row that is no part would run nothing, silently.
static const char *build_test_parts_of(const BuildTestScript *row) {
    if (strcmp(row->script, "test") != 0) return nullptr;
    return row->mode != nullptr && strcmp(row->mode, "isolate") == 0 ? row->shard : row->mode;
}

// Whether a row of tests/test.sh, plain (isolate false) or under --isolate, runs the part.
static bool build_part_runs(SmithBuild *b, const char *part, bool isolate) {
    for (size_t row = 0; row < build_test_script_count; row++) {
        const BuildTestScript *script = &build_test_scripts[row];
        const char *list = build_test_parts_of(script);
        bool is_isolate = script->mode != nullptr && strcmp(script->mode, "isolate") == 0;
        if (list != nullptr && is_isolate == isolate && build_list_has(b, list, ',', part)) return true;
    }
    return false;
}

static void build_check_test_parts(SmithBuild *b) {
    const char *parts = smith_capture(b, (const char *[]){"sh", "tests/test.sh", "--parts", nullptr});
    for (const char *name = parts; *name != '\0';) {
        size_t length = strcspn(name, "\n");
        const char *part = smith_fmt(b, "%.*s", (int)length, name);
        if (!build_part_runs(b, part, true))
            build_stop(b, smith_fmt(b, "part '%s' of tests/test.sh is in no isolate row of build_test_scripts", part));
        if (!build_part_runs(b, part, false))
            build_stop(b, smith_fmt(b, "part '%s' of tests/test.sh is in no plain row of build_test_scripts", part));
        name += length + (name[length] == '\n' ? 1 : 0);
    }
    for (size_t row = 0; row < build_test_script_count; row++) {
        const BuildTestScript *script = &build_test_scripts[row];
        for (const char *name = build_test_parts_of(script); name != nullptr && *name != '\0';) {
            size_t length = strcspn(name, ",");
            const char *part = smith_fmt(b, "%.*s", (int)length, name);
            if (!build_list_has(b, parts, '\n', part))
                build_stop(
                    b, smith_fmt(b, "task test-%s names '%s', which is no part of tests/test.sh", script->task, part));
            name += length + (name[length] == ',' ? 1 : 0);
        }
    }
}

// smith's own tests of behavior, in `.smith` on its test runner (issue #31): every `tests/*.smith` linked into one
// runner, built once under AddressSanitizer and UBSan, whose workers run the tests in process, one per core, where a
// script starts smith for each case. It is a project of its own (tests/units/build.c, in `<folder>/units-project`,
// whose `tests` is a link to ours) built and run by the smith under test, so the libraries and headers it compiles
// against are that smith's, whatever smith runs this build.c: adding a library needs no two-step install. `units-build`
// builds the runner (scripts that use it wait for that), `test-units` runs it with `smith test`. Needs a smith that
// compiles
// `.smith` (the stage0 has none). A test that needs a build or a process of its own (a stop, a sanitizer's catch) stays
// in its script. Returns the runner's path.
static const char *build_units(SmithBuild *b, const char *binary, const char *folder) {
    static const char setup[] =
        "repo=$PWD; p=$1/units-project; mkdir -p \"$p\" && ln -sfn \"$repo/tests\" \"$p/tests\" && "
        "{ cmp -s tests/units/build.c \"$p/build.c\" || cp tests/units/build.c \"$p/build.c\"; } && cd \"$p\" && ";
    const char *self = smith_fmt(b, "$repo/%s", binary);
    const char *environment =
        smith_fmt(b, "PATH=\"$repo/%s:$PATH\" SMITH_SELF=\"%s\" SMITH_REPO=\"$repo\"", folder, self);
    const char *inputs[] = {binary, smith_fmt(b, "%s/smith-cc", folder), nullptr};
    SmithStep build_step = {
        .command =
            (const char *[]){"sh", "-c", smith_fmt(b, "%s env %s \"%s\" build build/tests", setup, environment, self),
                             "sh", folder, nullptr},
        .inputs = inputs,
    };
    smith_task(b, "units-build", &build_step);
    SmithStep run = {
        .command = (const char *[]){"sh", "-c", smith_fmt(b, "%s env %s \"%s\" test", setup, environment, self), "sh",
                                    folder, nullptr},
        .inputs = (const char *[]){"units-build", nullptr},
        .test = true,
    };
    smith_task(b, "test-units", &run);
    return smith_fmt(b, "%s/units-project/build/tests", folder);
}

// The task of every script, each one of smith's tests (`smith test` runs them); returns the list, which the install
// waits for. Each script runs with SMITH_OUTPUT=full, what `smith check` and `smith test` set beneath them (#78): the
// scripts read the full output of the smith under test, so `smith build install` (which sets nothing) runs the same
// tests as `smith check`. The quiet form is tested with the variable unset (tests/run.sh).
static const char *const *build_test(SmithBuild *b, const BuildConfig *config, const char *binary, const char *folder) {
    build_check_test_parts(b);
    const char *units = config->hook ? build_units(b, binary, folder) : "";
    const char *names[build_test_script_count + 1];
    for (size_t i = 0; i < build_test_script_count; i++)
        names[i] = smith_fmt(b, "test-%s", build_test_scripts[i].task);
    for (size_t i = 0; i < build_test_script_count; i++) {
        const BuildTestScript *row = &build_test_scripts[i];
        const char *mode = row->mode != nullptr ? row->mode : "";
        SmithStep step = {
            .command = smith_cat(b,
                                 smith_list(b, "env", smith_fmt(b, "CC=%s", config->cc),
                                            smith_fmt(b, "UNITS=%s", units), "SMITH_OUTPUT=full", "sh",
                                            smith_fmt(b, "tests/%s.sh", row->script), binary, nullptr),
                                 smith_when(b, row->mode != nullptr, smith_list(b, row->mode, nullptr)),
                                 smith_when(b, row->shard != nullptr, smith_list(b, row->shard, nullptr)), nullptr),
            .description =
                smith_fmt(b, "tests/%s.sh %s %s", row->script, mode, row->shard != nullptr ? row->shard : ""),
            .inputs = (const char *[]){binary,
                                       units[0] != '\0' &&
                                               (strcmp(row->script, "json") == 0 || strcmp(row->script, "inflate") == 0)
                                           ? "units-build"
                                           : nullptr,
                                       nullptr},
            .test = true,
        };
        smith_task(b, names[i], &step);
    }
    names[build_test_script_count] = nullptr;
    const char *const *tasks =
        smith_cat(b, names, smith_when(b, units[0] != '\0', smith_list(b, "test-units", nullptr)), nullptr);
    // Every float through the parser, against strtof: minutes on all cores, so it is not part of `test`.
    SmithStep exhaustive = {
        .command = (const char *[]){"sh", "-c", "env CC=\"$0\" sh tests/text.sh \"$1\" exhaustive", config->cc, binary,
                                    nullptr},
        .inputs = (const char *[]){binary, nullptr},
    };
    smith_task(b, "text-exhaustive", &exhaustive);
    // The soundness harnesses of `smith prove` on 8 seeds of 50000 inputs each (issue #64).
    SmithStep sound_full = {
        .command = (const char *[]){"sh", "-c", "sh tests/prove_sound.sh \"$0\" full", binary, nullptr},
        .inputs = (const char *[]){binary, nullptr},
    };
    smith_task(b, "prove-sound-full", &sound_full);
    return tasks;
}

// One of smith's tests, `test-compare`: runs a table of invocations through a real compiler and through smith, and
// compares stdout, stderr, exit status and the files produced. REAL is the compiler: the clang of the LLVM that smith
// links, or Homebrew's on macOS for a smith without clang inside, else for example -DREAL=/usr/bin/clang.
static void build_compare(SmithBuild *b, const BuildConfig *config, const char *binary) {
    SmithStep compare = {
        .command =
            config->real[0] != '\0'
                ? (const char *[]){"sh", "tests/compare.sh", binary, config->real, nullptr}
                : (const char *[]){"sh", "-c", "echo 'usage: smith build test-compare -DREAL=/path/to/clang'; exit 2",
                                   nullptr},
        .inputs = (const char *[]){binary, nullptr},
        .test = true,
    };
    smith_task(b, "test-compare", &compare);
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
                         "ln -sf smith \"$0/bin/smith-c++\" && rm -f \"$0/bin/smith++\"";
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
// needs them), the token hook (#11), and the prover's libraries beside it. r8 is the eighth configuration
// (patches/*.patch); scripts/llvm.sh has the same name (SMITH_LLVM_NAME overrides both).
static const char *build_llvm_folder(SmithBuild *b) {
    const char *cache = smith_env(b, "XDG_CACHE_HOME", smith_fmt(b, "%s/.cache", smith_env(b, "HOME", "")));
    return smith_fmt(b, "%s/smith/llvm/%s", cache, smith_env(b, "SMITH_LLVM_NAME", "23.1.3-r8"));
}

// Enzyme beside the LLVM configurations, keyed by its version and the configuration it was built against, so that
// building it leaves the LLVM install alone (scripts/enzyme.sh).
static const char *build_enzyme_folder(SmithBuild *b) {
    const char *cache = smith_env(b, "XDG_CACHE_HOME", smith_fmt(b, "%s/.cache", smith_env(b, "HOME", "")));
    return smith_fmt(b, "%s/smith/enzyme/0.0.303-%s", cache, smith_env(b, "SMITH_LLVM_NAME", "23.1.3-r8"));
}

static const char *build_cmake_package(SmithBuild *b) {
    return smith_package(
        b, &(SmithPackage){
               .url = "https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-macos-universal.tar.gz",
               .hash = "sha256:4b7b73704b1db9b374e5c9ab8e17ac6148b817b6396ee75cd94a852cbac9d305"});
}

static const char *build_python_package(SmithBuild *b) {
    return smith_package(
        b, &(SmithPackage){.url = "https://github.com/astral-sh/python-build-standalone/releases/download/20261020/"
                                  "cpython-3.13.16%2B20261020-aarch64-apple-darwin-install_only.tar.gz",
                           .hash = "sha256:235f3d5b182a171f6564ee98ef2812cfb2da8a73c4a37fca1937b25702f94880"});
}

// Z3 and Alive2's libraries, built beside the LLVM (scripts/prove-libs.sh): both pinned by hash. Alive2 is the last
// commit before it moved to LLVM APIs that 23.1.3 does not have (its August 2026 commits to TargetLibraryInfo's
// getLibFunc and to nofree attributes), so it is the newest that builds against LLVM 23.1.3.
static int build_prove_libs_run(SmithRun *run, const char *ninja) {
    SmithBuild *b = run->b;
    const char *z3 = smith_package(
        b, &(SmithPackage){.url = "https://github.com/Z3Prover/z3/archive/refs/tags/z3-5.3.1.tar.gz",
                           .hash = "sha256:c007089dd8d92298d2a7d14105b14e8dddfbb540062ce471c3111e0741edd8da"});
    const char *alive2 = smith_package(
        b, &(SmithPackage){.url = "https://github.com/AliveToolkit/alive2/archive/"
                                  "01a5ec45c8152995755f7331827407a9de19f262.tar.gz",
                           .hash = "sha256:ea6d0ad5869a4c863ca537c65fe22e2f1198a5a1b2fc51d3a96b8020cf78cc13"});
    const char *install = smith_fmt(b, "%s/install", build_llvm_folder(b));
    const char *const argv[] = {
        "sh",   "scripts/prove-libs.sh", z3, alive2, build_cmake_package(b), build_python_package(b), install, ninja,
        nullptr};
    return smith_run_program(run, argv, nullptr);
}

// Bitwuzla decides the queries (scripts/bitwuzla.sh), with its own dependencies: GMP and MPFR, CaDiCaL and symfpu
// (which Meson would fetch itself), and Meson to build it. All pinned by hash, all built static, none from Homebrew.
static int build_bitwuzla_run(SmithRun *run, const char *ninja) {
    SmithBuild *b = run->b;
    const char *bitwuzla = smith_package(
        b, &(SmithPackage){.url = "https://github.com/bitwuzla/bitwuzla/archive/refs/tags/0.9.1.tar.gz",
                           .hash = "sha256:42707f38900a20bb18108e426ba667560d1fd2ccce0d4f75aa60439b546488b4"});
    const char *meson = smith_package(
        b, &(SmithPackage){.url = "https://github.com/mesonbuild/meson/releases/download/1.9.1/meson-1.9.1.tar.gz",
                           .hash = "sha256:4e076606f2afff7881d195574bddcd8d89286f35a17b4977a216f535dc0c74ac"});
    const char *gmp = smith_package(
        b, &(SmithPackage){.url = "https://ftp.gnu.org/gnu/gmp/gmp-6.3.0.tar.xz",
                           .hash = "sha256:a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898"});
    const char *mpfr = smith_package(
        b, &(SmithPackage){.url = "https://ftp.gnu.org/gnu/mpfr/mpfr-4.2.2.tar.xz",
                           .hash = "sha256:b67ba0383ef7e8a8563734e2e889ef5ec3c3b898a01d00fa0a6869ad81c6ce01"});
    const char *cadical = smith_package(
        b, &(SmithPackage){.url = "https://github.com/arminbiere/cadical/archive/rel-2.1.2.tar.gz",
                           .hash = "sha256:292c2bb8d712d6d05fce3d3df63b922b8fa45e03974a79f7bae5bf68c284f131"});
    const char *symfpu = smith_package(
        b, &(SmithPackage){.url = "https://github.com/martin-cs/symfpu/archive/"
                                  "40bdec00e99f8ea1b96c3dac0a05eed11c541639.tar.gz",
                           .hash = "sha256:ba17877fbf0c851e113fddaab225152f1c0b2044429396b56b2f113832e36ce5"});
    const char *const argv[] = {
        "sh",   "scripts/bitwuzla.sh", bitwuzla, meson, build_python_package(b), ninja, gmp, mpfr, cadical, symfpu,
        nullptr};
    return smith_run_program(run, argv, nullptr);
}

// Enzyme v0.0.303 (Apache-2.0 with the LLVM exception), pinned by hash and built static beside the LLVM install by
// scripts/enzyme.sh, with patches/enzyme/*.patch applied to a copy.
static int build_enzyme_run(SmithRun *run, const char *ninja) {
    SmithBuild *b = run->b;
    const char *enzyme = smith_package(
        b, &(SmithPackage){.url = "https://github.com/EnzymeAD/Enzyme/archive/refs/tags/v0.0.303.tar.gz",
                           .hash = "sha256:0ca3114d008e524cd3b13152bbc172794b20af643d1628161655373eba4554de"});
    const char *const argv[] = {
        "sh",  "scripts/enzyme.sh",    enzyme, build_cmake_package(b), smith_fmt(b, "%s/install", build_llvm_folder(b)),
        ninja, build_enzyme_folder(b), nullptr};
    return smith_run_program(run, argv, nullptr);
}

// `smith build enzyme`: only Enzyme, for an LLVM that is already built (`smith build llvm` runs it last too).
static int build_enzyme_only(SmithRun *run) {
    int status = build_enzyme_run(run, smith_fmt(run->b, "%s/tools/ninja", build_llvm_folder(run->b)));
    if (status != 0) return status;
    const char *const done[] = {"sh", "-c", ": >\"$0\"", run->outputs[0], nullptr};
    return smith_run_program(run, done, nullptr);
}

// The pinned LLVM (`smith build llvm`): the sources, CMake and Python by hash, built once into the package cache by
// scripts/llvm.sh, and then the prover's libraries beside it (Ninja, Z3, Alive2, Bitwuzla and its dependencies). A .run
// step, so the packages are fetched only when this target runs, not on every smith build.
static int build_llvm_run(SmithRun *run) {
    SmithBuild *b = run->b;
    const char *ninja_source = smith_package(
        b, &(SmithPackage){.url = "https://github.com/ninja-build/ninja/archive/refs/tags/v1.13.2.tar.gz",
                           .hash = "sha256:974d6b2f4eeefa25625d34da3cb36bdcebe7fbce40f4c16ac0835fd1c0cbae17"});
    const char *source = smith_package(
        b,
        &(SmithPackage){
            .url =
                "https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.3/llvm-project-23.1.3.src.tar.xz",
            .hash = "sha256:c44186a7762ed28954be72e5ff6df9808e0779d4f1bf014ecc4e7e211d31ee34"});
    const char *folder = build_llvm_folder(b);
    const char *ninja = smith_fmt(b, "%s/tools/ninja", folder);
    const char *stamp = smith_fmt(b, "%s.llvm", run->outputs[0]);
    const char *const make_ninja[] = {"sh", "scripts/ninja.sh", ninja_source, build_python_package(b), ninja, nullptr};
    int status = smith_run_program(run, make_ninja, nullptr);
    if (status != 0) return status;
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
    const char *const done[] = {"sh", "-c", "cp \"$1\" \"$2\"", "sh", stamp, run->outputs[0], nullptr};
    return smith_run_program(run, done, nullptr);
}

static void build_llvm(SmithBuild *b) {
    SmithStep llvm = {
        .run = build_llvm_run,
        .description = "building LLVM 23.1.3 and the prover's libraries (static, from source)",
        .inputs =
            (const char *[]){"scripts/llvm.sh", "scripts/defer_init.c", "scripts/ninja.sh", "scripts/prove-libs.sh",
                             "scripts/bitwuzla.sh", "scripts/enzyme.sh", "patches/enzyme/quiet-caching-call.patch",
                             "patches/clang-token-hook.patch", "patches/clang-debug-name.patch",
                             "patches/clang-format-smith.patch", "patches/smith-editor-defaults.patch",
                             "patches/smith-format-source-extension.patch", "patches/smith-command-hook.patch",
                             "patches/compiler-rt-xnu-fast-mmap.patch", nullptr},
        .outputs = (const char *[]){"build/llvm.stamp", nullptr},
    };
    smith_step(b, &llvm);
    smith_alias(b, "llvm", llvm.outputs);
    SmithStep enzyme = {
        .run = build_enzyme_only,
        .description = "building Enzyme (static, against the LLVM install)",
        .inputs = (const char *[]){"scripts/enzyme.sh", "patches/enzyme/quiet-caching-call.patch", nullptr},
        .outputs = (const char *[]){"build/enzyme.stamp", nullptr},
    };
    smith_step(b, &enzyme);
    smith_alias(b, "enzyme", enzyme.outputs);
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
           access(smith_fmt(b, "%s/lib/liblldMachO.a", llvm), R_OK) == 0 &&
           access(smith_fmt(b, "%s/lib/libsmithtools.a", llvm), R_OK) == 0 &&
           access(smith_fmt(b, "%s/lib/smith-clang.id", llvm), R_OK) == 0;
}

// The LLVM install that `smith build llvm` makes, which smith links clang from (smith always compiles in its own
// process). The folder is named even when nothing is built there yet: the default target says so, the others work.
static const char *build_llvm_install(SmithBuild *b) {
    return build_variable(b, "LLVM", smith_fmt(b, "%s/install", build_llvm_folder(b)),
                          "the LLVM install smith links clang from");
}

// Z3 and Alive2 built beside the LLVM install (scripts/prove-libs.sh puts them in ../prove of the install); "" when
// there are none, and the smith has no `smith prove`.
static const char *build_prove_folder(SmithBuild *b, const char *llvm) {
    const char *folder = smith_fmt(b, "%s/../prove", llvm);
    bool linkable = access(smith_fmt(b, "%s/lib/libz3.a", folder), R_OK) == 0 &&
                    access(smith_fmt(b, "%s/lib/libbitwuzla.a", folder), R_OK) == 0 &&
                    access(smith_fmt(b, "%s/lib/libalive2_ir.a", folder), R_OK) == 0;
    return linkable ? folder : "";
}

// Enzyme's static library, built beside the LLVM install (scripts/enzyme.sh); "" when there is none, and the smith
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
        .cc = build_variable(b, "CC", "smith-cc", "the smith that compiles smith: it compiles the .smith libraries"),
        .prefix = build_variable(b, "PREFIX", smith_fmt(b, "%s/.local", smith_env(b, "HOME", "")), "install here"),
        .werror = werror,
        .llvm = build_llvm_install(b),
    };
    config.linkable = build_llvm_linkable(b, config.llvm);
    config.llvm_id =
        smith_capture_or(b, (const char *[]){"cat", smith_fmt(b, "%s/lib/smith-clang.id", config.llvm), nullptr}, "");
    // Not the environment's CXX, which may be smith-c++ itself: the shim wants LLVM's own headers and a clang.
    // The compiler `compare` checks smith against: the clang of the LLVM smith links (it must match on every case).
    config.real = build_variable(b, "REAL", smith_fmt(b, "%s/bin/clang", config.llvm),
                                 "the compiler `compare` checks smith against");
    config.cxx = smith_option_value(b, "CXX", smith_fmt(b, "%s/bin/clang++", config.llvm), "compiles shim/clang.cpp");
    config.prove = build_prove_folder(b, config.llvm);
    config.hook = build_has_hook(b, config.llvm);
    config.enzyme = build_enzyme_install(b);
    config.lldb = access(smith_fmt(b, "%s/lib/libsmithlldb.a", config.llvm), R_OK) == 0;
    config.cflags = smith_cat(
        b, (const char *[]){smith_fmt(b, "-std=%s", std), "-O1", "-g", "-Wall", "-Wextra", "-Wpedantic", nullptr},
        smith_when(b, werror[0] != '\0', (const char *[]){werror, nullptr}), nullptr);

    SmithStep smith = build_programs(b, &config, folder);
    build_other_objects(b, &config, folder, build_test_modules(b, &config, folder));
    build_unbuilt_sources(b, &config, folder);
    build_protocol(b, &config, folder);
    config.binary = smith.outputs[0];
    build_headers(b, &config);
    const char *const *tested = build_test(b, &config, config.binary, folder);
    build_compare(b, &config, config.binary);
    build_install(b, &config, config.binary, tested);
    build_release(b, config.binary);
    build_llvm(b);
    SmithStep clean = {.command = (const char *[]){"rm", "-rf", "build", nullptr}};
    smith_task(b, "clean", &clean);
    // clangd reads this: the exact command of each compile, and build.c's own.
    smith_compile_commands(b, "compile_commands.json");
}
