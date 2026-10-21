// The build of smith, run with `smith build`. It replaces the Makefile and runs the commands the Makefile ran.
//
// Builds smith with any C23 compiler: smith.c is one file and needs nothing but libc. The standard is gnu23 (GNU mode
// keeps realpath and readlink declared on Linux), so it needs a clang with constexpr, nullptr and [[noreturn]]; a
// recent one has them.
//
// smith builds itself, so the installed smith must have `smith build`. A fresh clone, or an installed smith that is
// broken, needs only a C23 compiler:
//   mkdir -p build && cc -std=gnu23 smith.c -o build/stage0 && build/stage0 build install -DCC=cc
//
// Verbs, which run the target of their name: smith test, smith check (check-format, lint, test and compare), smith fmt
// (rewrites the sources) and smith lint. Targets of smith build: the default, text-exhaustive, compare -DREAL=...,
// check-format, install (-DCC=..., -DPREFIX=...), llvm and clean.
#include <smith/build.h>

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
} BuildConfig;

// A make variable set with ?=: -DNAME=value, else the environment, else the fallback.
static const char *build_variable(SmithBuild *b, const char *name, const char *fallback, const char *description) {
    return smith_option_value(b, name, smith_env(b, name, fallback), description);
}

// Formatting and lint are smith's own: `smith fmt` and `smith lint` of the smith this build makes, which are
// clang-format and clang-tidy of the LLVM linked into it (a stage0 runs Homebrew's, $SMITH_CLANG_FORMAT and
// $SMITH_CLANG_TIDY). The text is for sh, inside double quotes.
static const char *build_smith_tool(SmithBuild *b, const char *binary, const char *tool) {
    return smith_fmt(b, "\"%s\" %s", binary, tool);
}

// The tools must be one major version, so every machine formats and lints alike. The version is the one smith links
// (scripts/llvm.sh), or the one Homebrew's pinned tools are for a stage0; it is also named in AGENTS.md.
static SmithStep build_check_llvm(SmithBuild *b, const char *binary) {
    SmithStep check_llvm = {
        .command =
            (const char *[]){"sh", "-c",
                             smith_fmt(b,
                                       "for tool in fmt lint; do "
                                       "major=$(\"%s\" $tool --version 2>/dev/null | sed -n 's/.*version "
                                       "\\([0-9][0-9]*\\)\\..*/\\1/p' | head -n 1); "
                                       "if [ \"$major\" != 23 ]; then echo \"smith $tool: need LLVM 23, found "
                                       "${major:-none}; build smith with LLVM (smith build llvm), or for a stage0 "
                                       "brew install llvm\"; exit 1; fi; done",
                                       binary),
                             nullptr},
        .inputs = (const char *[]){binary, nullptr},
    };
    smith_task(b, "check-llvm", &check_llvm);
    return check_llvm;
}

// Rewrites the sources in the style of .clang-format (format), or fails on any file clang-format would change
// (check-format; `smith fmt` fixes them).
static void build_format(SmithBuild *b, const char *format, const char *const *llvm, const char *const *sources) {
    // The C files of the `smith new` templates follow the template's own rules, which are smith's, comments aside.
    const char *new_files = "templates/new/build.c templates/new/main.c";
    SmithStep rewrite = {
        .command =
            smith_cat(b,
                      (const char *[]){"sh", "-c",
                                       smith_fmt(b, "%s -i \"$@\" && %s -i --style=file:templates/new/clang-format %s",
                                                 format, format, new_files),
                                       "sh", nullptr},
                      sources, nullptr),
        .inputs = llvm,
    };
    smith_task(b, "fmt", &rewrite);
    SmithStep check_format = {
        .command = smith_cat(
            b,
            (const char *[]){
                "sh", "-c",
                smith_fmt(
                    b,
                    "%s --dry-run --Werror \"$@\" && %s --dry-run --Werror --style=file:templates/new/clang-format %s",
                    format, format, new_files),
                "sh", nullptr},
            sources, nullptr),
        .inputs = llvm,
    };
    smith_task(b, "check-format", &check_format);
}

// clang-tidy (.clang-tidy) over each source with the flags the build uses, against Xcode's SDK. build.c is tidied too,
// with -I include, where it finds <smith/build.h>. The `smith new` templates are tidied with their own rules
// and their own standard, gnu2y.
static void build_tidy(SmithBuild *b, const char *tidy, const char *const *llvm, const char *const *cflags) {
    const char *sdk = "-isysroot \"$(xcrun --show-sdk-path)\"";
    SmithStep check_tidy = {
        .command =
            smith_cat(b,
                      (const char *[]){"sh", "-c",
                                       smith_fmt(b,
                                                 "%s --quiet smith.c -- \"$@\" %s && "
                                                 "%s --quiet smith.c -- \"$@\" -DSMITH_IN_PROCESS "
                                                 "-DSMITH_CLANG_ID='\"llvm-0 0\"' %s && "
                                                 "%s --quiet runtime/pack_data.c -- \"$@\" "
                                                 "-DSMITH_CLANG_RESOURCE='\"tests/stub.c\"' %s && "
                                                 "%s --quiet scripts/pack.c -- \"$@\" %s && "
                                                 "%s --quiet scripts/defer_init.c -- \"$@\" %s && "
                                                 "%s --quiet build.c -- \"$@\" -I include %s && "
                                                 "%s --quiet runtime/build_runtime.c -- \"$@\" -I include %s && "
                                                 "%s --quiet tests/stub.c -- \"$@\" -DNAME='\"stub\"' %s && "
                                                 "%s --quiet lib/arena.c -- \"$@\" -I include %s && "
                                                 "%s --quiet tests/arena.c -- \"$@\" -I include %s && "
                                                 "%s --quiet lib/io.c -- \"$@\" -I include %s && "
                                                 "%s --quiet tests/io.c -- \"$@\" -I include %s && "
                                                 "%s --quiet lib/http.c -- \"$@\" -I include %s && "
                                                 "%s --quiet tests/http.c -- \"$@\" -I include %s && "
                                                 "%s --quiet lib/random.c -- \"$@\" -I include %s && "
                                                 "%s --quiet tests/random.c -- \"$@\" -I include %s && "
                                                 "%s --quiet tests/untar.c -- \"$@\" -I include %s && "
                                                 "%s --quiet lib/contract.c -- \"$@\" -I include %s && "
                                                 "%s --quiet lib/text.c -- \"$@\" -I include %s && "
                                                 "%s --quiet tests/text.c -- \"$@\" -I include %s && "
                                                 "%s --quiet tests/text_exhaustive.c -- \"$@\" -I include %s && "
                                                 "%s --quiet lib/json.c -- \"$@\" -I include %s && "
                                                 "%s --quiet tests/json.c -- \"$@\" -I include %s && "
                                                 "%s --quiet lib/test.c -- \"$@\" -I include %s && "
                                                 "%s --quiet lib/trace.c -- \"$@\" -I include %s && "
                                                 "for f in build main; do %s --quiet "
                                                 "--config-file=templates/new/clang-tidy templates/new/$f.c -- "
                                                 "\"$@\" -std=gnu2y -I include %s || exit 1; done",
                                                 tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy,
                                                 sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk,
                                                 tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy,
                                                 sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk),
                                       "sh", nullptr},
                      cflags, nullptr),
        .inputs = llvm,
    };
    smith_task(b, "lint", &check_tidy);
}

// Formatting, lint, the routing tests and the comparison with the real compiler.
// The files `smith fmt` checks: smith's own sources, its libraries and their tests.
static const char *const build_format_sources[] = {"smith.c",
                                                   "shim/clang.cpp",
                                                   "shim/prove.cpp",
                                                   "shim/prove.hpp",
                                                   "shim/prove_alive.cpp",
                                                   "shim/prove_bitwuzla.cpp",
                                                   "shim/prove_ir.cpp",
                                                   "shim/format.cpp",
                                                   "shim/tidy.cpp",
                                                   "shim/clangd.cpp",
                                                   "shim/symbolize.cpp",
                                                   "shim/lldb.cpp",
                                                   "shim/lldb_dap.cpp",
                                                   "tests/dap_client.c",
                                                   "scripts/pack.c",
                                                   "scripts/pack.h",
                                                   "scripts/defer_init.c",
                                                   "runtime/pack_data.c",
                                                   "build.c",
                                                   "tests/stub.c",
                                                   "runtime/build_runtime.c",
                                                   "runtime/package.c",
                                                   "runtime/gzip.c",
                                                   "runtime/xz.c",
                                                   "runtime/untar.c",
                                                   "runtime/cache.c",
                                                   "runtime/symbolize.c",
                                                   "runtime/executor.c",
                                                   "runtime/executor_manifest.c",
                                                   "runtime/executor_state.c",
                                                   "runtime/executor_plan.c",
                                                   "runtime/executor_run.c",
                                                   "runtime/executor_tools.c",
                                                   "include/smith/build.h",
                                                   "include/smith/arena.h",
                                                   "lib/arena.c",
                                                   "tests/arena.c",
                                                   "include/smith/io.h",
                                                   "lib/io.c",
                                                   "tests/io.c",
                                                   "include/smith/http.h",
                                                   "lib/http.c",
                                                   "tests/http.c",
                                                   "include/smith/random.h",
                                                   "lib/random.c",
                                                   "tests/random.c",
                                                   "tests/untar.c",
                                                   "include/smith/test.h",
                                                   "include/smith/contract.h",
                                                   "lib/contract.c",
                                                   "include/smith/text.h",
                                                   "lib/text.c",
                                                   "tests/text.c",
                                                   "tests/text_exhaustive.c",
                                                   "include/smith/json.h",
                                                   "lib/json.c",
                                                   "tests/json.c",
                                                   "lib/test.c",
                                                   "include/smith/trace.h",
                                                   "lib/trace.c",
                                                   nullptr};

static void build_checks(SmithBuild *b, const BuildConfig *config) {
    const char *format = build_smith_tool(b, config->binary, "fmt");
    const char *tidy = build_smith_tool(b, config->binary, "lint");
    SmithStep check_llvm = build_check_llvm(b, config->binary);
    build_format(b, format, check_llvm.outputs, build_format_sources);
    build_tidy(b, tidy, check_llvm.outputs, config->cflags);
    smith_alias(b, "check", (const char *[]){"check-format", "lint", "test", "compare", nullptr});
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

// shim/clang.cpp, which gives smith.c clang's main. The LLVM headers are system headers, so the build's warnings stay
// smith's own.
static void build_shim(SmithBuild *b, const BuildConfig *config, const char *folder, const char *source,
                       const char *name, SmithObject *shim) {
    *shim = (SmithObject){
        .compiler = config->cxx,
        .source = source,
        .output = smith_fmt(b, "%s/%s.o", folder, name),
        .flags =
            smith_cat(b,
                      (const char *[]){"-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-fno-exceptions", "-fno-rtti",
                                       "-D__STDC_CONSTANT_MACROS", "-D__STDC_FORMAT_MACROS", "-D__STDC_LIMIT_MACROS",
                                       "-isystem", smith_fmt(b, "%s/include", config->llvm), nullptr},
                      smith_when(b, config->werror[0] != '\0', (const char *[]){config->werror, nullptr}), nullptr),
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

// Everything smith links beside smith.c when clang is inside: the packed data, the shims of the compiler, the linker
// and the tools, and the prover when its libraries are there. `list` gets pointers to them, ended by nullptr.
typedef struct {
    SmithObject objects[10];
    BuildProver prove;
} BuildShims;

static void build_shims(SmithBuild *b, const BuildConfig *config, const char *folder, const char *pack,
                        BuildShims *shims, const SmithObject **list) {
    build_pack_data(b, config, folder, pack, &shims->objects[0]);
    const char *const sources[] = {"clang", "lld", "format", "tidy", "clangd", "symbolize", "lldb", "lldb_dap"};
    size_t shim_count = config->lldb ? sizeof sources / sizeof sources[0] : 6;
    for (size_t i = 0; i < shim_count; i++) {
        build_shim(b, config, folder, smith_fmt(b, "shim/%s.cpp", sources[i]), smith_fmt(b, "%s_shim", sources[i]),
                   &shims->objects[i + 1]);
    }
    size_t count = 0;
    for (size_t i = 0; i < shim_count + 1; i++)
        list[count++] = &shims->objects[i];
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
static SmithStep build_programs(SmithBuild *b, const BuildConfig *config, const char *folder) {
    bool in_process = config->llvm[0] != '\0';
    const char *pack = in_process ? build_resource_pack(b, config, folder) : "";
    SmithObject object = {
        .compiler = config->cc,
        .source = "smith.c",
        .output = smith_fmt(b, "%s/smith.o", folder),
        .flags =
            smith_cat(b, config->cflags,
                      smith_when(b, in_process,
                                 (const char *[]){"-DSMITH_IN_PROCESS",
                                                  smith_fmt(b, "-DSMITH_CLANG_ID=\"%s\"", config->llvm_id), nullptr}),
                      smith_when(b, in_process && config->prove[0] != '\0', (const char *[]){"-DSMITH_PROVE", nullptr}),
                      smith_when(b, in_process && config->lldb, (const char *[]){"-DSMITH_LLDB", nullptr}), nullptr),
        .implicit_inputs = (const char *[]){"scripts/pack.h",
                                            "include/smith/build.h",
                                            "runtime/build_runtime.c",
                                            "runtime/package.c",
                                            "runtime/gzip.c",
                                            "runtime/xz.c",
                                            "runtime/untar.c",
                                            "runtime/cache.c",
                                            "runtime/symbolize.c",
                                            "include/smith/arena.h",
                                            "lib/arena.c",
                                            "include/smith/io.h",
                                            "lib/io.c",
                                            "include/smith/random.h",
                                            "lib/random.c",
                                            "include/smith/contract.h",
                                            "lib/contract.c",
                                            "include/smith/text.h",
                                            "lib/text.c",
                                            "include/smith/json.h",
                                            "lib/json.c",
                                            "include/smith/http.h",
                                            "lib/http.c",
                                            "include/smith/test.h",
                                            "lib/test.c",
                                            "templates/new/build.c",
                                            "templates/new/main.c",
                                            "templates/new/gitignore",
                                            "templates/new/clang-format",
                                            "templates/new/clang-tidy",
                                            "templates/new/claude-settings.json",
                                            nullptr},
    };
    smith_object(b, &object);
    BuildShims shims = {};
    const SmithObject *objects[18] = {&object};
    if (in_process) build_shims(b, config, folder, pack, &shims, objects + 1);
    SmithProgram program = {
        .linker = config->cc,
        .output = smith_fmt(b, "%s/smith", folder),
        .flags = smith_when(b, in_process, (const char *[]){"-Wl,-dead_strip", "-Wl,-exported_symbol,_main", nullptr}),
        .objects = objects,
        .libraries =
            in_process ? smith_cat(b, shims.prove.libraries != nullptr ? shims.prove.libraries : smith_list(b, nullptr),
                                   build_clang_libraries(b, config->llvm), build_lldb_libraries(b, config),
                                   (const char *[]){"-lc++", "-lm", "-lcompression", nullptr}, nullptr)
                       : nullptr,
    };
    smith_program(b, &program);
    smith_default(b, build_links(b, &program.step, folder));
    return program.step;
}

// The other two C files are not part of the program, but compiling them in a plain build checks that they build, and
// gives the editor their flags in compile_commands.json. They take the flags lint gives them: the runtime reads
// <smith/build.h>, and the stub is each stub compiler's source, named by -DNAME.
static void build_other_objects(SmithBuild *b, const BuildConfig *config, const char *folder) {
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
    };
    smith_object(b, &stub);
    smith_default(b, smith_cat(b, runtime.step.outputs, stub.step.outputs, nullptr));
}

// Routing tests with stub compilers (no clang or Xcode needed, and no real build is touched), then the `smith build`
// tests and the `smith new` tests, which use real ninja, clang and git. The runner's tests run twice, with its workers
// and with --isolate.
static SmithStep build_test(SmithBuild *b, const BuildConfig *config, const char *binary) {
    SmithStep test = {
        .command =
            (const char *[]){"sh", "-c",
                             // NOLINTNEXTLINE(bugprone-suspicious-missing-comma): one script, in two literals
                             "for t in run build new arena io http package random text json cache untar resource dap "
                             "defer contract prove test; do env "
                             "\"$0\" sh tests/$t.sh "
                             "\"$1\" || exit 1; done; "
                             "env \"$0\" sh tests/test.sh \"$1\" isolate",
                             smith_fmt(b, "CC=%s", config->cc), binary, nullptr},
        .inputs = (const char *[]){binary, nullptr},
    };
    smith_task(b, "test", &test);
    // Every float through the parser, against strtof: minutes on all cores, so it is not part of `test`.
    SmithStep exhaustive = {
        .command = (const char *[]){"sh", "-c", "env CC=\"$0\" sh tests/text.sh \"$1\" exhaustive", config->cc, binary,
                                    nullptr},
        .inputs = (const char *[]){binary, nullptr},
    };
    smith_task(b, "text-exhaustive", &exhaustive);
    return test;
}

// Runs a table of invocations through a real compiler and through smith, and compares stdout, stderr, exit status and
// the files produced. REAL is the compiler: the clang of the LLVM that smith links, or Homebrew's on macOS for a smith
// without clang inside, else for example -DREAL=$(command -v clang).
static void build_compare(SmithBuild *b, const BuildConfig *config, const char *binary) {
    SmithStep compare = {
        .command = config->real[0] != '\0'
                       ? (const char *[]){"sh", "tests/compare.sh", binary, config->real, nullptr}
                       : (const char *[]){"sh", "-c", "echo 'usage: smith build compare -DREAL=/path/to/clang'; exit 2",
                                          nullptr},
        .inputs = (const char *[]){binary, nullptr},
    };
    smith_task(b, "compare", &compare);
}

// Copies smith, once its tests pass, to $PREFIX/bin, with the links smith-cc and smith-c++ (and without the old
// smith++), where projects find it as CC. A link to the build folder would
// hand every project each half-finished build. The copy goes to a temporary name and the rename replaces the binary in
// one step, so a build running meanwhile never sees half a file.
static void build_install(SmithBuild *b, const BuildConfig *config, const char *binary, const char *const *tested) {
    const char *script = "mkdir -p \"$0/bin\" && cp \"$1\" \"$0/bin/smith.tmp\" && "
                         "mv \"$0/bin/smith.tmp\" \"$0/bin/smith\" && ln -sf smith \"$0/bin/smith-cc\" && "
                         "ln -sf smith \"$0/bin/smith-c++\" && rm -f \"$0/bin/smith++\"";
    SmithStep install = {
        .command = (const char *[]){"sh", "-c", script, config->prefix, binary, nullptr},
        .inputs = tested,
    };
    smith_task(b, "install", &install);
}

// The folder of the LLVM configuration under the package cache: one build of 23.1.3 with RTTI and exceptions on (Alive2
// needs them), the token hook (#11), and the prover's libraries beside it. r2 is the second configuration;
// scripts/llvm.sh has the same name (SMITH_LLVM_NAME overrides both).
static const char *build_llvm_folder(SmithBuild *b) {
    const char *cache = smith_env(b, "XDG_CACHE_HOME", smith_fmt(b, "%s/.cache", smith_env(b, "HOME", "")));
    return smith_fmt(b, "%s/smith/llvm/%s", cache, smith_env(b, "SMITH_LLVM_NAME", "23.1.3-r2"));
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
    const char *const done[] = {"sh", "-c", "cp \"$1\" \"$2\"", "sh", stamp, run->outputs[0], nullptr};
    return smith_run_program(run, done, nullptr);
}

static void build_llvm(SmithBuild *b) {
    SmithStep llvm = {
        .run = build_llvm_run,
        .description = "building LLVM 23.1.3 and the prover's libraries (static, from source)",
        .inputs =
            (const char *[]){"scripts/llvm.sh", "scripts/defer_init.c", "scripts/ninja.sh", "scripts/prove-libs.sh",
                             "scripts/bitwuzla.sh", "patches/clang-token-hook.patch", nullptr},
        .outputs = (const char *[]){"build/llvm.stamp", nullptr},
    };
    smith_step(b, &llvm);
    smith_alias(b, "llvm", llvm.outputs);
}

// The LLVM install that `smith build llvm` makes, which smith links clang from; "" when there is none (and on other
// systems than macOS, where it is not built), so that the plain smith, which runs an external clang, is built instead.
static const char *build_llvm_install(SmithBuild *b) {
    const char *llvm =
        build_variable(b, "LLVM", smith_fmt(b, "%s/install", build_llvm_folder(b)),
                       "the LLVM install smith links clang from; empty: a smith that runs an external clang");
    if (strcmp(b->os, "Darwin") != 0) return "";
    bool linkable = access(smith_fmt(b, "%s/lib/libclangmain.a", llvm), R_OK) == 0 &&
                    access(smith_fmt(b, "%s/lib/liblldMachO.a", llvm), R_OK) == 0 &&
                    access(smith_fmt(b, "%s/lib/libsmithtools.a", llvm), R_OK) == 0 &&
                    access(smith_fmt(b, "%s/lib/smith-clang.id", llvm), R_OK) == 0;
    return linkable ? llvm : "";
}

// Z3 and Alive2 built beside the LLVM install (scripts/prove-libs.sh puts them in ../prove of the install); "" when
// there are none, and the smith has no `smith prove`.
static const char *build_prove_folder(SmithBuild *b, const char *llvm) {
    if (llvm[0] == '\0') return "";
    const char *folder = smith_fmt(b, "%s/../prove", llvm);
    bool linkable = access(smith_fmt(b, "%s/lib/libz3.a", folder), R_OK) == 0 &&
                    access(smith_fmt(b, "%s/lib/libbitwuzla.a", folder), R_OK) == 0 &&
                    access(smith_fmt(b, "%s/lib/libalive2_ir.a", folder), R_OK) == 0;
    return linkable ? folder : "";
}

void build(SmithBuild *b) {
    const char *std = build_variable(b, "STD", "gnu23", "the C standard");
    const char *werror = build_variable(b, "WERROR", "-Werror", "-Werror, or empty to let warnings pass");
    const char *folder = build_variable(b, "BUILD", smith_fmt(b, "build/%s-%s", b->os, b->arch), "the build folder");
    BuildConfig config = {
        .cc = build_variable(b, "CC", "cc", "the compiler that builds smith"),
        .prefix = build_variable(b, "PREFIX", smith_fmt(b, "%s/.local", smith_env(b, "HOME", "")), "install here"),
        .werror = werror,
        .llvm = build_llvm_install(b),
    };
    config.llvm_id =
        config.llvm[0] != '\0'
            ? smith_capture(b, (const char *[]){"cat", smith_fmt(b, "%s/lib/smith-clang.id", config.llvm), nullptr})
            : "";
    // Not the environment's CXX, which may be smith-c++ itself: the shim wants LLVM's own headers and a clang.
    // The compiler `compare` checks smith against: the clang of the LLVM smith links (it must match on every case),
    // else Homebrew's, the one a smith without clang inside runs on macOS. Homebrew's differs from it in its version
    // string.
    const char *brew = strcmp(b->os, "Darwin") == 0 ? "/opt/homebrew/opt/llvm/bin/clang" : "";
    config.real = build_variable(b, "REAL", config.llvm[0] != '\0' ? smith_fmt(b, "%s/bin/clang", config.llvm) : brew,
                                 "the compiler `compare` checks smith against");
    config.cxx = smith_option_value(b, "CXX", smith_fmt(b, "%s/bin/clang++", config.llvm), "compiles shim/clang.cpp");
    config.prove = build_prove_folder(b, config.llvm);
    config.lldb = config.llvm[0] != '\0' && access(smith_fmt(b, "%s/lib/libsmithlldb.a", config.llvm), R_OK) == 0;
    config.cflags = smith_cat(
        b, (const char *[]){smith_fmt(b, "-std=%s", std), "-O1", "-g", "-Wall", "-Wextra", "-Wpedantic", nullptr},
        smith_when(b, werror[0] != '\0', (const char *[]){werror, nullptr}), nullptr);

    SmithStep smith = build_programs(b, &config, folder);
    build_other_objects(b, &config, folder);
    config.binary = smith.outputs[0];
    SmithStep test = build_test(b, &config, config.binary);
    build_compare(b, &config, config.binary);
    build_checks(b, &config);
    build_install(b, &config, config.binary, test.outputs);
    build_llvm(b);
    SmithStep clean = {.command = (const char *[]){"rm", "-rf", "build", nullptr}};
    smith_task(b, "clean", &clean);
    // clangd reads this: the exact command of each compile, and build.c's own.
    smith_compile_commands(b, "compile_commands.json");
}
