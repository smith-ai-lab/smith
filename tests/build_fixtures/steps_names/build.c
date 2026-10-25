#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep step = {.command = (const char *[]){"sh", "-c", "echo ran >> log", nullptr}};
    smith_task(b, smith_option_value(b, "name", "check/src/a.c", "a task name"), &step);
    smith_alias(b, "group/all", step.outputs);
    smith_default(b, (const char *[]){nullptr});
}
