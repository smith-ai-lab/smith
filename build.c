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
// Targets: smith build, test, text-exhaustive, check, compare -DREAL=..., format, install (-DCC=..., -DPREFIX=...) and
// clean.
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
    const char *cxx;     // compiles shim/clang.cpp, the only C++
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
// (check-format; `smith build format` fixes them).
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
    smith_task(b, "format", &rewrite);
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
                      (const char *[]){
                          "sh", "-c",
                          smith_fmt(b,
                                    "%s --quiet smith.c -- \"$@\" %s && "
                                    "%s --quiet smith.c -- \"$@\" -DSMITH_IN_PROCESS "
                                    "-DSMITH_CLANG_RESOURCE='\"tests/stub.c\"' -DSMITH_CLANG_ID='\"llvm-0 0\"' %s && "
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
                                    "%s --quiet lib/text.c -- \"$@\" -I include %s && "
                                    "%s --quiet tests/text.c -- \"$@\" -I include %s && "
                                    "%s --quiet tests/text_exhaustive.c -- \"$@\" -I include %s && "
                                    "%s --quiet lib/test.c -- \"$@\" -I include %s && "
                                    "for f in build main; do %s --quiet "
                                    "--config-file=templates/new/clang-tidy templates/new/$f.c -- "
                                    "\"$@\" -std=gnu2y -I include %s || exit 1; done",
                                    tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy,
                                    sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk,
                                    tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk, tidy, sdk),
                          "sh", nullptr},
                      cflags, nullptr),
        .inputs = llvm,
    };
    smith_task(b, "check-tidy", &check_tidy);
}

// Formatting, lint, the routing tests and the comparison with the real compiler.
static void build_checks(SmithBuild *b, const BuildConfig *config) {
    const char *format = build_smith_tool(b, config->binary, "fmt");
    const char *tidy = build_smith_tool(b, config->binary, "lint");
    SmithStep check_llvm = build_check_llvm(b, config->binary);
    const char *const *sources = (const char *[]){"smith.c",
                                                  "shim/clang.cpp",
                                                  "shim/format.cpp",
                                                  "shim/tidy.cpp",
                                                  "shim/clangd.cpp",
                                                  "scripts/pack.c",
                                                  "scripts/pack.h",
                                                  "scripts/defer_init.c",
                                                  "build.c",
                                                  "tests/stub.c",
                                                  "runtime/build_runtime.c",
                                                  "runtime/package.c",
                                                  "runtime/gzip.c",
                                                  "runtime/xz.c",
                                                  "runtime/untar.c",
                                                  "runtime/cache.c",
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
                                                  "include/smith/text.h",
                                                  "lib/text.c",
                                                  "tests/text.c",
                                                  "tests/text_exhaustive.c",
                                                  "lib/test.c",
                                                  nullptr};
    build_format(b, format, check_llvm.outputs, sources);
    build_tidy(b, tidy, check_llvm.outputs, config->cflags);
    smith_alias(b, "check", (const char *[]){"check-format", "check-tidy", "test", "compare", nullptr});
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
    const char *const *more = smith_list(b, smith_fmt(b, "%s/lib/libclangdSupport.a", llvm),
                                         smith_fmt(b, "%s/lib/libclangdRemoteIndex.a", llvm),
                                         smith_fmt(b, "%s/lib/libclangmain.a", llvm),
                                         smith_fmt(b, "%s/lib/liblldMachO.a", llvm),
                                         smith_fmt(b, "%s/lib/liblldCommon.a", llvm), nullptr);
    return smith_cat(b, first, found, more, nullptr);
}

// shim/clang.cpp, which gives smith.c clang's main. The LLVM headers are system headers, so the build's warnings stay
// smith's own.
static SmithObject build_shim(SmithBuild *b, const BuildConfig *config, const char *folder, const char *source,
                              const char *name) {
    SmithObject shim = {
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

// The program, and the same program under another name, as clang++ is to clang: it compiles and links C++ with
// clang++. The build folder is named for the OS and architecture, so a binary built on one machine is never mistaken
// for an up-to-date one on another (this folder is shared with a Linux VM as well as macOS). smith.c embeds the build
// header, the runtime and the `smith new` files, so a change to any recompiles it, though the depfile may not list
// them. With an LLVM install (`smith build llvm`), smith.c is built with SMITH_IN_PROCESS, and the shim and clang's
// libraries are linked in, so that smith compiles in its own process.
static SmithStep build_programs(SmithBuild *b, const BuildConfig *config, const char *folder) {
    bool in_process = config->llvm[0] != '\0';
    const char *pack = in_process ? build_resource_pack(b, config, folder) : "";
    SmithObject object = {
        .compiler = config->cc,
        .source = "smith.c",
        .output = smith_fmt(b, "%s/smith.o", folder),
        .flags = smith_cat(
            b, config->cflags,
            smith_when(b, in_process,
                       (const char *[]){"-DSMITH_IN_PROCESS", smith_fmt(b, "-DSMITH_CLANG_RESOURCE=\"%s\"", pack),
                                        smith_fmt(b, "-DSMITH_CLANG_ID=\"%s\"", config->llvm_id), nullptr}),
            nullptr),
        .implicit_inputs = (const char *[]){"scripts/pack.h",
                                            pack,
                                            "include/smith/build.h",
                                            "runtime/build_runtime.c",
                                            "runtime/package.c",
                                            "runtime/gzip.c",
                                            "runtime/xz.c",
                                            "runtime/untar.c",
                                            "runtime/cache.c",
                                            "include/smith/arena.h",
                                            "lib/arena.c",
                                            "include/smith/io.h",
                                            "lib/io.c",
                                            "include/smith/random.h",
                                            "lib/random.c",
                                            "include/smith/text.h",
                                            "lib/text.c",
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
    SmithObject shim = in_process ? build_shim(b, config, folder, "shim/clang.cpp", "clang_shim") : (SmithObject){};
    SmithObject lld = in_process ? build_shim(b, config, folder, "shim/lld.cpp", "lld_shim") : (SmithObject){};
    SmithObject format = in_process ? build_shim(b, config, folder, "shim/format.cpp", "format_shim") : (SmithObject){};
    SmithObject tidy = in_process ? build_shim(b, config, folder, "shim/tidy.cpp", "tidy_shim") : (SmithObject){};
    SmithObject clangd = in_process ? build_shim(b, config, folder, "shim/clangd.cpp", "clangd_shim") : (SmithObject){};
    SmithProgram program = {
        .linker = config->cc,
        .output = smith_fmt(b, "%s/smith", folder),
        .flags = smith_when(b, in_process, (const char *[]){"-Wl,-dead_strip", nullptr}),
        .objects = in_process ? (const SmithObject *[]){&object, &shim, &lld, &format, &tidy, &clangd, nullptr}
                              : (const SmithObject *[]){&object, nullptr},
        .libraries = in_process ? smith_cat(b, build_clang_libraries(b, config->llvm),
                                            (const char *[]){"-lc++", "-lm", "-lcompression", nullptr}, nullptr)
                                : nullptr,
    };
    smith_program(b, &program);
    SmithStep smith_plus = {
        .command = (const char *[]){"ln", "-sf", "smith", "$out", nullptr},
        .inputs = program.step.outputs,
        .outputs = (const char *[]){smith_fmt(b, "%s/smith++", folder), nullptr},
    };
    smith_step(b, &smith_plus);
    smith_default(b, smith_cat(b, program.step.outputs, smith_plus.outputs, nullptr));
    return program.step;
}

// The other two C files are not part of the program, but compiling them in a plain build checks that they build, and
// gives the editor their flags in compile_commands.json. They take the flags check-tidy gives them: the runtime reads
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
            (const char *[]){
                "sh", "-c",
                // NOLINTNEXTLINE(bugprone-suspicious-missing-comma): one script, in two literals
                "for t in run build new arena io http package random text cache untar resource defer test; do env "
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

// Copies smith, once its tests pass, to $PREFIX/bin, where projects find it as CC. A link to the build folder would
// hand every project each half-finished build. The copy goes to a temporary name and the rename replaces the binary in
// one step, so a build running meanwhile never sees half a file.
static void build_install(SmithBuild *b, const BuildConfig *config, const char *binary, const char *const *tested) {
    const char *script = "mkdir -p \"$0/bin\" && cp \"$1\" \"$0/bin/smith.tmp\" && "
                         "mv \"$0/bin/smith.tmp\" \"$0/bin/smith\" && ln -sf smith \"$0/bin/smith++\"";
    SmithStep install = {
        .command = (const char *[]){"sh", "-c", script, config->prefix, binary, nullptr},
        .inputs = tested,
    };
    smith_task(b, "install", &install);
}

// The pinned LLVM (`smith build llvm`): sources, CMake and Python by hash, built once into the package cache by
// scripts/llvm.sh. A .run step, so the packages are fetched only when this target runs, not on every smith build.
static int build_llvm_run(SmithRun *run) {
    SmithBuild *b = run->b;
    const char *source = smith_package(
        b,
        &(SmithPackage){
            .url =
                "https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.3/llvm-project-23.1.3.src.tar.xz",
            .hash = "sha256:c44186a7762ed28954be72e5ff6df9808e0779d4f1bf014ecc4e7e211d31ee34"});
    const char *cmake = smith_package(
        b, &(SmithPackage){
               .url = "https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-macos-universal.tar.gz",
               .hash = "sha256:4b7b73704b1db9b374e5c9ab8e17ac6148b817b6396ee75cd94a852cbac9d305"});
    const char *python = smith_package(
        b, &(SmithPackage){.url = "https://github.com/astral-sh/python-build-standalone/releases/download/20261020/"
                                  "cpython-3.13.16%2B20261020-aarch64-apple-darwin-install_only.tar.gz",
                           .hash = "sha256:235f3d5b182a171f6564ee98ef2812cfb2da8a73c4a37fca1937b25702f94880"});
    const char *const argv[] = {"sh", run->inputs[0], source, cmake, python, run->outputs[0], nullptr};
    return smith_run_program(run, argv, nullptr);
}

static void build_llvm(SmithBuild *b) {
    SmithStep llvm = {
        .run = build_llvm_run,
        .description = "building LLVM 23.1.3 (static, from source)",
        .inputs = (const char *[]){"scripts/llvm.sh", "scripts/defer_init.c", nullptr},
        .outputs = (const char *[]){"build/llvm.stamp", nullptr},
    };
    smith_step(b, &llvm);
    smith_alias(b, "llvm", llvm.outputs);
}

// The LLVM install that `smith build llvm` makes, which smith links clang from; "" when there is none (and on other
// systems than macOS, where it is not built), so that the plain smith, which runs an external clang, is built instead.
static const char *build_llvm_install(SmithBuild *b) {
    const char *cache = smith_env(b, "XDG_CACHE_HOME", smith_fmt(b, "%s/.cache", smith_env(b, "HOME", "")));
    const char *llvm =
        build_variable(b, "LLVM", smith_fmt(b, "%s/smith/llvm/23.1.3/install", cache),
                       "the LLVM install smith links clang from; empty: a smith that runs an external clang");
    if (strcmp(b->os, "Darwin") != 0) return "";
    bool linkable = access(smith_fmt(b, "%s/lib/libclangmain.a", llvm), R_OK) == 0 &&
                    access(smith_fmt(b, "%s/lib/liblldMachO.a", llvm), R_OK) == 0 &&
                    access(smith_fmt(b, "%s/lib/libsmithtools.a", llvm), R_OK) == 0 &&
                    access(smith_fmt(b, "%s/lib/smith-clang.id", llvm), R_OK) == 0;
    return linkable ? llvm : "";
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
    // Not the environment's CXX, which may be smith++ itself: the shim wants LLVM's own headers and a clang.
    // The compiler `compare` checks smith against: the clang of the LLVM smith links (it must match on every case),
    // else Homebrew's, the one a smith without clang inside runs on macOS. Homebrew's differs from it in its version
    // string.
    const char *brew = strcmp(b->os, "Darwin") == 0 ? "/opt/homebrew/opt/llvm/bin/clang" : "";
    config.real = build_variable(b, "REAL", config.llvm[0] != '\0' ? smith_fmt(b, "%s/bin/clang", config.llvm) : brew,
                                 "the compiler `compare` checks smith against");
    config.cxx = smith_option_value(b, "CXX", smith_fmt(b, "%s/bin/clang++", config.llvm), "compiles shim/clang.cpp");
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
