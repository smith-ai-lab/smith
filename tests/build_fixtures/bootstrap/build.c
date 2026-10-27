#include <smith/build.h>

#include <stddef.h>

// A stage-1-style build: late/value.smith uses a construct this compiler lacks (`@@` is no token of C), main.c calls
// it. -Dstage1 compiles it as a bootstrap object and links leniently; without it, the compile fails the build.
void build(SmithBuild *b) {
    bool stage1 = smith_option(b, "stage1", "compile as stage 1 of a staged build");
    const char *const flags[] = {"-std=gnu23", "-g0", nullptr};
    static SmithObject objects[2];
    static const SmithObject *pointers[3];
    const char *const sources[] = {"src/main.c", "src/late/value.smith"};
    for (size_t i = 0; i < 2; i++) {
        objects[i] = (SmithObject){
            .compiler = "bin/smith-cc",
            .source = sources[i],
            .output = smith_fmt(b, "build/%s.o", sources[i]),
            .flags = flags,
            .bootstrap = stage1,
        };
        smith_object(b, &objects[i]);
        pointers[i] = &objects[i];
    }
    SmithProgram app = {.linker = "bin/smith-cc", .output = "build/app", .objects = pointers, .lenient = stage1};
    smith_program(b, &app);
    smith_default(b, app.step.outputs);
}
