// The build of @NAME@, run with `smith build`. smith new wrote it, and every line is yours to change.
//   smith build             the debug program, build/debug/@NAME@
//   smith build release     the optimized program with assertions on, build/release/@NAME@
//   smith build fast        the optimized program with assertions off, build/fast/@NAME@
//   smith build -t clean    removes what was built
// The verbs do the same thing in every project and read what this file declares, so there is nothing to write for them:
//   smith run     builds the program smith_default names, below, and runs it; its arguments go to the program
//   smith test    builds the tests smith_tests declares, below, and runs them
//   smith fmt     formats every C file git sees to .clang-format (.clang-format-ignore leaves files out)
//   smith lint    runs clang-tidy (.clang-tidy) on the sources compiled below
//   smith docs    checks AGENTS.md, knowledge/ and the skills in .agents/skills
//   smith prove   proves the contracts (<smith/contract.h>) in the sources compiled below; with none, nothing to prove
//   smith check   fails when formatting, the docs, lint, a test or a proof would
// A task of your own goes here and runs with `smith build <task>`; no task may be named after a verb.
#include <smith/build.h>

#include <string.h>

// C2y with every warning, and warnings as errors. C2y is the draft standard after C23; it is here for _Countof, which
// counts an array's elements, and upstream clang 21 or later accepts it. If a compiler you need does
// not, use gnu23. GNU mode (gnu2y) keeps POSIX functions declared on Linux.
static const char *const build_warnings[] = {"-std=gnu2y",
                                             "-Wall",
                                             "-Wextra",
                                             "-Wpedantic",
                                             "-Wshadow",
                                             "-Wconversion",
                                             "-Wimplicit-fallthrough",
                                             "-Wmissing-prototypes",
                                             "-Wstrict-prototypes",
                                             "-Wvla",
                                             "-Werror",
                                             nullptr};

// While developing: debug information, no optimization, and the sanitizers, so the first out-of-bounds access, use
// after free or undefined behaviour stops the program where it happens. A forgotten initialization reads a pattern,
// the same on every run.
static const char *const build_debug[] = {"-g",
                                          "-O0",
                                          "-fsanitize=address,undefined",
                                          "-fno-sanitize-recover=all",
                                          "-fno-omit-frame-pointer",
                                          "-ftrivial-auto-var-init=pattern",
                                          nullptr};

// The program you ship: optimized, and assertions stay on. A broken invariant stops the program instead of corrupting
// its data, and an assertion costs little next to what a bug costs.
static const char *const build_release[] = {"-O2", nullptr};

// The same, with assertions off (-DNDEBUG), for when a measurement shows they cost too much. Measure before you use it.
static const char *const build_fast[] = {"-O2", "-DNDEBUG", nullptr};

// The program's sources. Add a file here when you add one, and the program, `smith lint` and `smith prove` take it.
static const char *const build_sources[] = {"main.c", "primes.c", nullptr};

// One program: the sources compiled with flags, then linked with link_flags, into build/<variant>/.
static SmithStep build_program(SmithBuild *b, const char *variant, const char *const *flags,
                               const char *const *link_flags) {
    constexpr size_t count = _Countof(build_sources) - 1;
    SmithObject objects[count] = {};
    const SmithObject *linked[count + 1] = {};
    for (size_t i = 0; i < count; i++) {
        objects[i] = (SmithObject){
            .compiler = "smith-cc",
            .source = build_sources[i],
            .output = smith_replace_extension(b, smith_fmt(b, "build/%s/%s", variant, build_sources[i]), ".o"),
            .flags = flags,
        };
        smith_object(b, &objects[i]);
        linked[i] = &objects[i];
    }
    SmithProgram program = {
        .linker = "smith-cc",
        .output = smith_fmt(b, "build/%s/@NAME@", variant),
        .flags = link_flags,
        .objects = linked,
    };
    smith_program(b, &program);
    return program.step;
}

// The tests, which `smith test` runs: the files in tests/ whose functions are named test_<name>, built with the debug
// flags and run on the primes. smith_tests links smith's test library and the libraries it needs, and the code under
// test goes in .objects.
static void build_tests(SmithBuild *b, const char *const *flags) {
    SmithObject primes = {
        .compiler = "smith-cc",
        .source = "primes.c",
        .output = "build/test/primes.o",
        .flags = flags,
    };
    smith_object(b, &primes);
    SmithTests tests = {
        .sources = smith_glob(b, smith_list(b, "tests/test_*.c", nullptr)),
        .objects = (const SmithObject *[]){&primes, nullptr},
        .flags = smith_cat(b, flags, smith_list(b, "-I", ".", "-I", ".smith/include", nullptr), nullptr),
        .link_flags = smith_list(b, "-fsanitize=address,undefined", nullptr),
    };
    smith_tests(b, &tests);
}

void build(SmithBuild *b) {
    // Clang modules: every #include of a system header, of a smith library's, and of a header in .folders becomes an
    // import, parsed once and cached once per machine in ~/.cache/smith/modules, so the build gets faster as it grows.
    // The source stays plain C. When you add a folder of headers, such as "include", list it here:
    // .folders = (const char *[]){"include", nullptr}. A header that only works after another include is a compile
    // error here; make it include what it uses, or list it in .textual. Delete this call to turn modules off.
    smith_modules(b, &(SmithModules){});

    // Floating point gives the same bits whatever the compiler and the optimization. On Apple silicon the CPU is named:
    // apple-m1 is the baseline every Apple silicon Mac runs, where -mcpu=native would tune the program to this machine
    // and could crash it on an older one. Elsewhere the compiler's own baseline applies.
    bool apple_silicon = strcmp(b->os, "Darwin") == 0 && strcmp(b->arch, "arm64") == 0;
    const char *const *common =
        smith_cat(b, build_warnings, (const char *[]){"-ffp-contract=off", nullptr},
                  smith_when(b, apple_silicon, (const char *[]){"-mcpu=apple-m1", nullptr}), nullptr);

    // Debug first, so the editor reads the debug flags from compile_commands.json.
    SmithStep debug = build_program(b, "debug", smith_cat(b, common, build_debug, nullptr),
                                    (const char *[]){"-fsanitize=address,undefined", nullptr});
    SmithStep release = build_program(b, "release", smith_cat(b, common, build_release, nullptr), nullptr);
    SmithStep fast = build_program(b, "fast", smith_cat(b, common, build_fast, nullptr), nullptr);

    build_tests(b, smith_cat(b, common, build_debug, nullptr));
    smith_alias(b, "release", release.outputs);
    smith_alias(b, "fast", fast.outputs);
    smith_default(b, debug.outputs);
    smith_compile_commands(b, "compile_commands.json");
}
