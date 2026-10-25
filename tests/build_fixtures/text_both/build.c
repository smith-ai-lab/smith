#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu23", "-Wall", "-Wextra", "-Werror", "-I", ".smith/include", nullptr);
    SmithObject text = {.compiler = "smith-cc", .source = smith_library(b, "text"), .output = "build/text.o", .flags = flags};
    smith_object(b, &text);
    SmithObject from_smith = {.compiler = "smith-cc", .source = "src/main.smith", .output = "build/main.o", .flags = flags};
    smith_object(b, &from_smith);
    SmithObject from_c = {.compiler = "smith-cc", .source = "src/c.c", .output = "build/c.o", .flags = flags,
                          .implicit_inputs = smith_list(b, ".smith/include/smith/text.h", nullptr)};
    smith_object(b, &from_c);
    SmithProgram program = {.linker = "smith-cc", .output = "build/prog",
                            .objects = (const SmithObject *[]){&text, &from_smith, &from_c, nullptr}};
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
}
