#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep step = {.command = (const char *[]){"echo", smith_library(b, "nope"), nullptr}};
    smith_task(b, "x", &step);
}
