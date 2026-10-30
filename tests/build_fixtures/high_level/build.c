#include <smith/build.h>
#include <string.h>

void build(SmithBuild *b) {
    SmithObject main_object = {
        .compiler = "cc",
        .source = "src dir/main.c",
        .output = "build/main.o",
        .flags = (const char *[]){"-std=gnu23", "-DGREETING=\"hi\"", nullptr},
    };
    smith_object(b, &main_object);
    SmithObject util = {.compiler = "cc", .source = "util.c", .output = "build/util.o"};
    smith_object(b, &util);
    SmithProgram app = {
        .linker = "cc",
        .output = "build/app",
        .objects = (const SmithObject *[]){&main_object, &util, nullptr},
        .libraries = strcmp(b->os, "Darwin") == 0
                         ? (const char *[]){"-lm", "-framework", "Foundation", "libextra.a", nullptr}
                         : (const char *[]){"-lm", "libextra.a", nullptr}, // -framework is macOS's
    };
    smith_program(b, &app);
    smith_compile_commands(b, "compile_commands.json");
    smith_default(b, app.step.outputs);
}
