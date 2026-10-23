#include <smith/build.h>

#include <stddef.h>
#include <string.h>

static SmithObject objects[64];
static const SmithObject *pointers[65];

void build(SmithBuild *b) {
    const char *const flags[] = {"-std=gnu23", "-g0", "-Wno-unused-function", nullptr};
    const char *const *sources = smith_glob(b, (const char *[]){"src/**/*.smith", "src/*.c", nullptr});
    size_t count = 0;
    for (; sources[count] != nullptr; count++) {
        objects[count] = (SmithObject){
            .compiler = "bin/smith-cc",
            .source = sources[count],
            .output = smith_fmt(b, "build/%s.o", sources[count]),
            .flags = flags,
        };
        if (strcmp(sources[count], "src/checksum.c") == 0) { // C reads the header of a module
            objects[count].flags = smith_cat(b, flags, smith_list(b, "-I", ".smith/include", nullptr), nullptr);
            objects[count].implicit_inputs = smith_list(b, ".smith/include/game/replay.h", nullptr);
        }
        smith_object(b, &objects[count]);
        pointers[count] = &objects[count];
    }
    SmithProgram app = {.linker = "bin/smith-cc", .output = "build/app", .objects = pointers};
    smith_program(b, &app);
    smith_compile_commands(b, "compile_commands.json");
    smith_default(b, app.step.outputs);
}
