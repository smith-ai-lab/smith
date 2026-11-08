#include <smith/build.h>
#include <stddef.h>

// A tree that looks like smith's own (smith.c, build.c, runtime/executor_run.c) and whose one step fails: the failure
// is what makes `smith build` look at how old the smith running it is (#2787).
void build(SmithBuild *b) {
    SmithStep step = {
        .command = (const char *[]){"sh", "-c", "exit 1", nullptr},
        .outputs = (const char *[]){"never.out", nullptr},
    };
    smith_step(b, &step);
    smith_default(b, step.outputs);
}
