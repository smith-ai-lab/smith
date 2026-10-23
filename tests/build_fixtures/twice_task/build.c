#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep first = {.command = (const char *[]){"true", nullptr}};
    smith_task(b, "tool", &first);
    SmithStep second = {.command = (const char *[]){"false", nullptr}};
    smith_task(b, "tool", &second);
    smith_default(b, first.outputs);
}
