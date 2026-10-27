#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu23", "-Wall", "-Werror", "-fsanitize=address", "-I", ".smith/include", nullptr);
    SmithObject main = {.compiler = "smith-cc", .source = "main.c", .output = "build/main.o", .flags = flags};
    smith_object(b, &main);
    SmithObject random = {.compiler = "smith-cc", .source = smith_library(b, "random"), .output = "build/smith/random.o", .flags = flags};
    smith_object(b, &random);
    SmithObject arena = {.compiler = "smith-cc", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
    smith_object(b, &arena);
    SmithObject categorical = {.compiler = "smith-cc", .source = smith_library(b, "categorical"), .output = "build/smith/categorical.o", .flags = flags};
    smith_object(b, &categorical);
    SmithProgram program = {
        .linker = "smith-cc",
        .output = "build/app",
        .flags = smith_list(b, "-fsanitize=address", nullptr),
        .objects = (const SmithObject *[]){&main, &random, &arena, &categorical, nullptr},
    };
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
    smith_compile_commands(b, "compile_commands.json");
}
