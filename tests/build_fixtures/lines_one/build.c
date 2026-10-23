#include <smith/build.h>
#include <stddef.h>

void build(SmithBuild *b) {
    const char *const *lines = smith_lines(b, "data");
    SmithStep step = {
        .command = (const char *[]){"sh", "-c", "echo \"$0\" > \"$1\"", lines[0], "result", nullptr},
        .outputs = (const char *[]){"result", nullptr},
    };
    smith_step(b, &step);
    smith_default(b, step.outputs);
}
