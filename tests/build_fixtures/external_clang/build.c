#include <smith/build.h>

void build(SmithBuild *b) {
    SmithObject a = {.compiler = "smith-bin/smith-cc", .source = "a.c", .output = "build/a.o"};
    smith_object(b, &a);
    smith_default(b, a.step.outputs);
}
