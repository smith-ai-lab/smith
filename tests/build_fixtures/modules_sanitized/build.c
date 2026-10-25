#include <smith/build.h>

void build(SmithBuild *b) {
    smith_modules(b, &(SmithModules){.folders = (const char *[]){"inc", nullptr}});
    SmithObject plain = {.compiler = "cc", .source = "p.c", .output = "build/plain.o", .flags = smith_list(b, "-std=gnu23", nullptr)};
    SmithObject asan = {.compiler = "cc", .source = "q.c", .output = "build/asan.o", .flags = smith_list(b, "-std=gnu23", "-fsanitize=address,undefined", nullptr)};
    SmithObject metal = {.compiler = "echo", .source = "k.metal", .output = "build/k.o"};
    smith_object(b, &plain);
    smith_object(b, &asan);
    smith_object(b, &metal);
    SmithProgram app = {.linker = "cc", .output = "build/app", .flags = smith_list(b, "-fsanitize=address,undefined", nullptr), .objects = (const SmithObject *[]){&plain, &asan, nullptr}};
    smith_program(b, &app);
    smith_compile_commands(b, "compile_commands.json");
    smith_default(b, app.step.outputs);
}
