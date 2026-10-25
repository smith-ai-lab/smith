#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu23", "-Wall", "-Werror", "-I", ".smith/include", nullptr);
    SmithObject main = {.compiler = "smith-cc", .source = "main.c", .output = "build/main.o", .flags = flags};
    smith_object(b, &main);
    SmithObject io = {.compiler = "smith-cc", .source = smith_library(b, "io"), .output = "build/smith/io.o", .flags = flags};
    smith_object(b, &io);
    SmithProgram program = {
        .linker = "smith-cc",
        .output = "build/app",
        .objects = (const SmithObject *[]){&main, &io, nullptr},
    };
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
}
