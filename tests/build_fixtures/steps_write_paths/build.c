#include <smith/build.h>

void build(SmithBuild *b) {
    smith_write(b, smith_option_value(b, "path", "ok.txt", "where to write"), "x");
    SmithStep step = {.command = (const char *[]){"true", nullptr}, .outputs = (const char *[]){"y", nullptr}};
    smith_step(b, &step);
    smith_default(b, step.outputs);
}
