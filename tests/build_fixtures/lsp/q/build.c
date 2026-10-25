#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const flags[] = {"-std=gnu2y", "-Wall", "-Wextra", "-I", ".smith/include", nullptr};
    SmithObject vec = {.compiler = "smith-cc", .source = "geom/vec.smith", .output = "build/vec.o", .flags = flags};
    SmithObject user = {.compiler = "smith-cc", .source = "user.smith", .output = "build/user.o", .flags = flags};
    SmithObject c_object = {.compiler = "smith-cc", .source = "c_use.c", .output = "build/c_use.o", .flags = flags};
    smith_object(b, &vec);
    smith_object(b, &user);
    smith_object(b, &c_object);
    SmithProgram program = {.linker = "smith-cc", .output = "build/demo", .objects = (const SmithObject *[]){&vec, &user, &c_object, nullptr}};
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
    smith_compile_commands(b, "compile_commands.json");
}
