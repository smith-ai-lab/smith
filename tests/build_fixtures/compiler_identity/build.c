#include <smith/build.h>

void build(SmithBuild *b) {
    SmithObject a = {.compiler = "fake/smith-cc", .source = "a.c", .output = "build/a.o"};
    smith_object(b, &a);
    smith_default(b, a.step.outputs);
}
