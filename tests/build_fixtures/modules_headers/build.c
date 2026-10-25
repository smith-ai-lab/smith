#include <smith/build.h>

#include <stddef.h>

void build(SmithBuild *b) {
    smith_modules(b, &(SmithModules){});
    const char *const *files = smith_glob(b, smith_list(b, "*_*.c", nullptr));
    const char *const *objects = smith_map(b, files, "build/%s.o");
    for (size_t i = 0; files[i]; i++) {
        SmithObject o = {.compiler = "cc", .source = files[i], .output = objects[i],
                         .flags = smith_list(b, "-std=gnu23", "-Wall", "-Werror", "-I", ".smith/include", nullptr)};
        smith_object(b, &o);
    }
    smith_compile_commands(b, "compile_commands.json");
    smith_default(b, objects);
}
