#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu23", "-Wall", "-Werror", "-fsanitize=address", "-I", ".smith/include", nullptr);
    SmithObject main = {.compiler = "smith-cc", .source = "main.c", .output = "build/main.o", .flags = flags};
    smith_object(b, &main);
    SmithObject io = {.compiler = "smith-cc", .source = smith_library(b, "io"), .output = "build/smith/io.o", .flags = flags};
    smith_object(b, &io);
    SmithObject random = {.compiler = "smith-cc", .source = smith_library(b, "random"), .output = "build/smith/random.o", .flags = flags};
    smith_object(b, &random);
    SmithObject arena = {.compiler = "smith-cc", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
    smith_object(b, &arena);
    SmithProgram program = {
        .linker = "smith-cc",
        .output = "build/app",
        .flags = smith_list(b, "-fsanitize=address", nullptr),
        .objects = (const SmithObject *[]){&main, &io, &random, &arena, nullptr},
    };
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
    smith_compile_commands(b, "compile_commands.json");
}
