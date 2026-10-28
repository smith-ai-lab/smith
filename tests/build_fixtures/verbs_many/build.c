#include <smith/build.h>
#include <stdio.h>

// 8200 programs, more than the 8192 `smith run`, `lint` and `prove` once kept of each kind (issue #604); only the last
// is the default, so only it is built. One object serves all of them: declaring thousands of objects is slow.
constexpr int count = 8200;
static SmithProgram programs[count];

void build(SmithBuild *b) {
    static SmithObject main = {.compiler = "smith-cc", .source = "main.c", .output = "build/main.o"};
    smith_object(b, &main);
    static const SmithObject *objects[] = {&main, nullptr};
    for (int i = 0; i < count; i++) {
        char output[64];
        (void)snprintf(output, sizeof output, "build/p%d", i);
        programs[i] = (SmithProgram){.linker = "smith-cc", .output = smith_list(b, output, nullptr)[0], .objects = objects};
        smith_program(b, &programs[i]);
    }
    smith_default(b, programs[count - 1].step.outputs);
}
