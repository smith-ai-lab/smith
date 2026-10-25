#include <smith/build.h>

void build(SmithBuild *b) {
    smith_modules(b, &(SmithModules){.folders = (const char *[]){"inc", nullptr}});
    SmithObject main = {.compiler = "cc", .source = "main.c", .output = "build/main.o", .flags = smith_list(b, "-std=gnu23", nullptr)};
    SmithObject value = {.compiler = "cc", .source = "value.c", .output = "build/value.o", .flags = smith_list(b, "-std=gnu23", nullptr)};
    smith_object(b, &main);
    smith_object(b, &value);
    SmithProgram app = {.linker = "cc", .output = "build/app", .objects = (const SmithObject *[]){&main, &value, nullptr}};
    smith_program(b, &app);
    smith_default(b, app.step.outputs);
}
