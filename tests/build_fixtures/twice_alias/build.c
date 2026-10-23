#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep step = {.command = (const char *[]){"true", nullptr}};
    smith_task(b, "tool", &step);
    smith_alias(b, "group", step.outputs);
    smith_alias(b, "group", (const char *[]){nullptr});
    smith_default(b, step.outputs);
}
