#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const flags[] = {"-std=gnu2y", "-I", "inc", "-I", ".smith/include", nullptr};
    const char *const a_flags[] = {"-std=gnu2y", "-I", "inc", "-I", ".smith/include", "-DA", nullptr};
    const char *const b_flags[] = {"-std=gnu2y", "-I", "inc", "-I", ".smith/include", "-DB", nullptr};
    SmithObject shared = {.compiler = "smith-cc", .source = "shared.c", .output = "build/shared.o", .flags = flags};
    SmithObject a = {.compiler = "smith-cc", .source = "a.c", .output = "build/a.o", .flags = a_flags};
    SmithObject b_object = {.compiler = "smith-cc", .source = "b.c", .output = "build/b.o", .flags = b_flags};
    smith_object(b, &shared);
    smith_object(b, &a);
    smith_object(b, &b_object);
    SmithProgram first = {.linker = "smith-cc", .output = "build/first",
                          .objects = (const SmithObject *[]){&shared, &a, nullptr}};
    SmithProgram second = {.linker = "smith-cc", .output = "build/second",
                           .objects = (const SmithObject *[]){&shared, &b_object, nullptr}};
    smith_program(b, &first);
    smith_program(b, &second);
    smith_default(b, smith_cat(b, first.step.outputs, second.step.outputs, nullptr));
}
