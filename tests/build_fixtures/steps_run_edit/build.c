#include <smith/build.h>

static int write_it(SmithRun *run) {
    smith_write(run->b, run->outputs[0], smith_fmt(run->b, "v1 %s\n", run->args[0]));
    return 0;
}

static int deferred(SmithRun *run) {
    smith_write(run->b, run->outputs[0], "from defer\n");
    return 0;
}

static void more(SmithBuild *b) {
    SmithStep step = {.run = deferred, .outputs = (const char *[]){"deferred.out", nullptr}};
    smith_step(b, &step);
    smith_default(b, step.outputs);
}

void build(SmithBuild *b) {
    const char *who = smith_option_value(b, "who", "nobody", "a name");
    SmithStep step = {
        .run = write_it,
        .args = (const char *[]){who, nullptr},
        .outputs = (const char *[]){"it.out", nullptr},
    };
    smith_step(b, &step);
    smith_alias(b, "it", step.outputs);
    smith_defer(b, (const char *[]){"it.out", nullptr}, more);
}
