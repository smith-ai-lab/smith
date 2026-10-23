#include <smith/build.h>

void build(SmithBuild *b) {
    const char *compiler = smith_option_value(b, "COMPILER", "bin/cc1", "the compiler");
    SmithObject a = {.compiler = compiler, .source = "a.c", .output = "build/a.o"};
    SmithObject other = {.compiler = compiler, .source = "b.c", .output = "build/b.o"};
    smith_object(b, &a);
    smith_object(b, &other);
    SmithProgram program = {.linker = "bin/cc3", .output = "build/program", .objects = (const SmithObject *[]){&a, &other, nullptr}};
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
}
