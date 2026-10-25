#include <smith/build.h>

void build(SmithBuild *b) {
    const char *text = smith_capture(b, (const char *[]){"false", nullptr});
    SmithStep step = {.command = (const char *[]){"echo", text, nullptr}};
    smith_task(b, "t", &step);
    smith_default(b, step.outputs);
}
