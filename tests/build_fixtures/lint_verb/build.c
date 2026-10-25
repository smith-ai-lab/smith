#include <smith/build.h>

void build(SmithBuild *b) {
    SmithObject object = {.compiler = "smith-cc", .source = "main.smith", .output = "build/main.o"};
    smith_object(b, &object);
    smith_default(b, object.step.outputs);
}
