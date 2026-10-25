#include <smith/build.h>

static int first(SmithRun *run) {
    (void)run;
    return 0;
}

static int second(SmithRun *run) {
    (void)run;
    return 0;
}

void build(SmithBuild *b) {
    SmithStep task = {.command = (const char *[]){"true", nullptr}};
    SmithStep verb = task;
    smith_task(b, "lint", &verb);
    smith_task(b, "other", &task);
    smith_alias(b, "other", task.outputs);
    smith_alias(b, "docs", task.outputs);
    SmithStep a = {.run = first, .description = "first check"};
    SmithStep c = {.run = second, .description = "first check"};
    smith_step(b, &a);
    smith_step(b, &c);
    smith_default(b, task.outputs);
}
