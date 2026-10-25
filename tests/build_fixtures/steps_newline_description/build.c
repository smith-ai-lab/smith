#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep step = {
        .command = (const char *[]){"true", nullptr},
        .outputs = (const char *[]){"x", nullptr},
        .description = "two\nlines",
    };
    smith_step(b, &step);
    smith_default(b, step.outputs);
}
