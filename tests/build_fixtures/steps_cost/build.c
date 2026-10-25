#include <smith/build.h>

static int trivial(SmithRun *run) {
    (void)run;
    return 0;
}

void build(SmithBuild *b) {
    SmithStep tick = {.run = trivial};
    smith_task(b, "tick", &tick);
    SmithStep tock = {.command = (const char *[]){"true", nullptr}};
    smith_task(b, "tock", &tock);
    smith_default(b, (const char *[]){nullptr});
}
