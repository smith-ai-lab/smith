#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu23", "-Wall", "-Werror", "-g", "-I", ".smith/include", nullptr);
    SmithObject main = {.compiler = "smith-cc", .source = "main.c", .output = "build/main.o", .flags = flags};
    smith_object(b, &main);
    SmithObject contract = {.compiler = "smith-cc", .source = smith_library(b, "contract"), .output = "build/smith/contract.o", .flags = flags};
    smith_object(b, &contract);
    SmithProgram program = {
        .linker = "smith-cc",
        .output = "build/app",
        .objects = (const SmithObject *[]){&main, &contract, nullptr},
    };
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
}
