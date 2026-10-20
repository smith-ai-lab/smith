// The build of @NAME@, run with `smith build`. smith new wrote it, and every line is yours to change.
//   smith build             the debug program, build/debug/@NAME@
//   smith build run         builds it and runs it
//   smith build release     the optimized program, build/release/@NAME@
//   smith build -t clean    removes what was built
#include <smith/build.h>

#include <string.h>

// C23 with every warning, and warnings as errors. GNU mode (gnu23) keeps POSIX functions declared on Linux.
static const char *const build_warnings[] = {"-std=gnu23",
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

// The program you ship: optimized, with assertions off.
static const char *const build_release[] = {"-O2", "-DNDEBUG", nullptr};

// One program: main.c compiled with flags, then linked with link_flags, into build/<variant>/.
static SmithStep build_program(SmithBuild *b, const char *variant, const char *const *flags,
                               const char *const *link_flags) {
    SmithObject object = {
        .compiler = "smith",
        .source = "main.c",
        .output = smith_fmt(b, "build/%s/main.o", variant),
        .flags = flags,
    };
    smith_object(b, &object);
    SmithProgram program = {
        .linker = "smith",
        .output = smith_fmt(b, "build/%s/@NAME@", variant),
        .flags = link_flags,
        .objects = (const SmithObject *[]){&object, nullptr},
    };
    smith_program(b, &program);
    return program.step;
}

void build(SmithBuild *b) {
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

    SmithStep run = {
        .command = debug.outputs,
        .inputs = debug.outputs,
        .pool = "console",
    };
    smith_task(b, "run", &run);
    smith_alias(b, "release", release.outputs);
    smith_default(b, debug.outputs);
    smith_compile_commands(b, "compile_commands.json");
}
