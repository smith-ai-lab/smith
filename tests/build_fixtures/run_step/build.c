#include <smith/build.h>
#include <stdio.h>

static int fail_three(SmithRun *run) {
    (void)run;
    fputs("the program's own message\n", stderr);
    return 3;
}

static int fail_unnamed(SmithRun *run) {
    (void)run;
    return 5;
}

// list.txt names the file to read; the step does not list that file as an input.
static int learn(SmithRun *run) {
    const char *const *lines = smith_lines(run->b, run->inputs[0]);
    smith_run_depend(run, lines[0]);
    FILE *log = fopen("learn.log", "a");
    fputs("ran\n", log);
    fclose(log);
    const char *const *data = smith_lines(run->b, lines[0]);
    smith_write(run->b, run->outputs[0], smith_fmt(run->b, "%s\n", data[0]));
    return 0;
}

static int learn_none(SmithRun *run) {
    smith_write(run->b, run->outputs[0], "none\n");
    return 0;
}

static int learn_orphan(SmithRun *run) {
    smith_run_depend(run, "x");
    return 0;
}

void build(SmithBuild *b) {
    SmithStep named = {.run = fail_three, .outputs = (const char *[]){"named.out", nullptr},
                       .description = "check the widget"};
    smith_step(b, &named);
    SmithStep unnamed = {.run = fail_unnamed, .outputs = (const char *[]){"unnamed.out", nullptr}};
    smith_step(b, &unnamed);
    SmithStep learn_step = {.run = learn,
                            .inputs = (const char *[]){"list.txt", nullptr},
                            .outputs = (const char *[]){"learned.out", nullptr},
                            .depfile = "build/learned.d"};
    smith_step(b, &learn_step);
    SmithStep none = {.run = learn_none, .outputs = (const char *[]){"none.out", nullptr}, .depfile = "build/none.d"};
    smith_step(b, &none);
    SmithStep orphan = {.run = learn_orphan, .stamp = "orphan.stamp"};
    smith_step(b, &orphan);
    smith_alias(b, "named", named.outputs);
    smith_alias(b, "unnamed", unnamed.outputs);
    smith_alias(b, "orphan", orphan.outputs);
    smith_default(b, smith_cat(b, learn_step.outputs, none.outputs, nullptr));
}
