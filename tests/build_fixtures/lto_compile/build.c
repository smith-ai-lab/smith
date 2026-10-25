#include <smith/build.h>
#include <stddef.h>

// A ThinLTO compile writes bitcode in the object's place; its effect summary is still one of its outputs.
void build(SmithBuild *b) {
    SmithObject main_object = {.compiler = "smith-cc",
                               .source = "main.c",
                               .output = "build/main.o",
                               .flags = (const char *[]){"-flto=thin", nullptr}};
    smith_object(b, &main_object);
    SmithProgram program = {.linker = "smith-cc",
                            .output = "build/program",
                            .objects = (const SmithObject *[]){&main_object, nullptr},
                            .flags = (const char *[]){"-flto=thin", nullptr}};
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
}
