#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep clean = {.command = (const char *[]){"rm", "-rf", "build", nullptr}};
    smith_task(b, "clean", &clean);
    SmithStep write = {
        .command = (const char *[]){"sh", "-c", "echo x > out", nullptr},
        .outputs = (const char *[]){"out", nullptr},
    };
    smith_step(b, &write);
    smith_default(b, write.outputs);
}
