#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const flags[] = {"-std=gnu2y", "-Wall", "-Wextra", "-Wpedantic", "-Werror", nullptr};
    SmithObject main_object = {.compiler = "smith-cc", .source = "main.smith", .output = "build/main.o", .flags = flags};
    SmithObject box_object = {.compiler = "smith-cc", .source = "geometry/box.smith", .output = "build/box.o", .flags = flags};
    SmithObject util_object = {.compiler = "smith-cc", .source = "util.c", .output = "build/util.o", .flags = flags};
    SmithObject pick_object = {.compiler = "smith-cc", .source = "pick.smith", .output = "build/pick.o", .flags = flags};
    smith_object(b, &main_object);
    smith_object(b, &box_object);
    smith_object(b, &util_object);
    smith_object(b, &pick_object);
    SmithObject twice_object = {.compiler = "smith-cc", .source = "m.smith", .output = "build/m.o", .flags = flags};
    SmithObject users_object = {.compiler = "smith-cc", .source = "users.smith", .output = "build/users.o", .flags = flags};
    smith_object(b, &twice_object);
    smith_object(b, &users_object);
    SmithObject same_object = {.compiler = "smith-cc", .source = "same.smith", .output = "build/same.o", .flags = flags};
    smith_object(b, &same_object);
    SmithObject sliced_object = {.compiler = "smith-cc", .source = "sliced.smith", .output = "build/sliced.o", .flags = flags};
    smith_object(b, &sliced_object);
    SmithProgram program = {.linker = "smith-cc", .output = "build/demo", .objects = (const SmithObject *[]){&main_object, &box_object, &util_object, &pick_object, &twice_object, &users_object, nullptr}};
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
    smith_compile_commands(b, "compile_commands.json");
}
