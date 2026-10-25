#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu23", "-Wall", "-Werror", "-fsanitize=address", "-I", ".smith/include", nullptr);
    SmithObject typed = {.compiler = "smith-cc", .source = "typed.smith", .output = "build/typed.o", .flags = flags};
    smith_object(b, &typed);
    SmithObject arena = {.compiler = "smith-cc", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
    smith_object(b, &arena);
    SmithProgram program = {
        .linker = "smith-cc",
        .output = "build/typed",
        .flags = smith_list(b, "-fsanitize=address", nullptr),
        .objects = (const SmithObject *[]){&typed, &arena, nullptr},
    };
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
}
