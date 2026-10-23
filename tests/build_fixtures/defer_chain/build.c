#include <smith/build.h>
#include <stddef.h>

static void last(SmithBuild *b) {
    const char *const *names = smith_lines(b, "second");
    SmithStep step = {
        .command = (const char *[]){"sh", "-c", "echo \"$0\" > \"$1\"", names[0], "final", nullptr},
        .inputs = (const char *[]){"second", nullptr},
        .outputs = (const char *[]){"final", nullptr},
    };
    smith_step(b, &step);
    smith_default(b, step.outputs);
}

static void middle(SmithBuild *b) {
    SmithStep step = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = (const char *[]){"list", nullptr},
        .outputs = (const char *[]){"second", nullptr},
    };
    smith_step(b, &step);
    smith_defer(b, step.outputs, last);
}

void build(SmithBuild *b) {
    SmithStep list = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = (const char *[]){"source", nullptr},
        .outputs = (const char *[]){"list", nullptr},
    };
    smith_step(b, &list);
    smith_defer(b, list.outputs, middle);
}
