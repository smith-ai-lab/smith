#include <smith/build.h>

void build(SmithBuild *b) {
    smith_modules(b, &(SmithModules){.folders = (const char *[]){"inc", nullptr}});
    const char *const flags[] = {"-std=gnu23", "-Wall", "-Werror", nullptr};
    const char *const names[] = {"x", "ab", "y", nullptr};
    SmithObject objects[3];
    const SmithObject *linked[4] = {};
    for (int i = 0; names[i]; i++) {
        objects[i] = (SmithObject){.compiler = "cc", .source = smith_fmt(b, "%s.c", names[i]),
                                   .output = smith_fmt(b, "build/%s.o", names[i]),
                                   .flags = smith_cat(b, flags, smith_list(b, smith_option_value(b, "EXTRA", "-DNONE", "a flag"), nullptr), nullptr)};
        smith_object(b, &objects[i]);
        linked[i] = &objects[i];
    }
    SmithProgram app = {.linker = "cc", .output = "build/app", .objects = linked};
    smith_program(b, &app);
    smith_compile_commands(b, "compile_commands.json");
    smith_default(b, app.step.outputs);
}
