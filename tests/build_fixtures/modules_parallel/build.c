#include <smith/build.h>

#include <stddef.h>

void build(SmithBuild *b) {
    smith_modules(b, &(SmithModules){.folders = (const char *[]){"inc", nullptr}});
    const char *const sources = "s*.c";
    const char *const *files = smith_glob(b, smith_list(b, sources, nullptr));
    const char *const *objects = smith_map(b, files, "build/%s.o");
    for (size_t i = 0; files[i]; i++) {
        SmithObject o = {.compiler = "cc", .source = files[i], .output = objects[i], .flags = smith_list(b, "-std=gnu23", nullptr)};
        smith_object(b, &o);
    }
    smith_default(b, objects);
}
