#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep hello = {.command = (const char *[]){"sh", "-c", "echo ran >> log", nullptr}};
    smith_task(b, "hello", &hello);
    SmithStep stamped = {
        .command = (const char *[]){"sh", "-c", "echo ran >> stamped_log", nullptr},
        .inputs = (const char *[]){"watched", nullptr},
    };
    smith_step(b, &stamped);
    smith_default(b, stamped.outputs);
}
