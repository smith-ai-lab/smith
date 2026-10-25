#include <smith/build.h>
#include <stdio.h>
#include <string.h>

static int first_word(SmithRun *run) {
    const char *const *lines = smith_lines(run->b, run->inputs[0]);
    smith_write(run->b, run->outputs[0], smith_fmt(run->b, "%s\n", lines[0]));
    return 0;
}

static int show(SmithRun *run) {
    FILE *log = fopen("runs.log", "a");
    for (const char *const *arg = run->args; *arg; arg++)
        fprintf(log, "%s ", *arg);
    fputc('\n', log);
    fclose(log);
    printf("task saw %s\n", run->args[0]);
    return 0;
}

static int fail_three(SmithRun *run) {
    (void)run;
    fputs("this step fails\n", stderr);
    return 3;
}

void build(SmithBuild *b) {
    SmithStep word = {
        .run = first_word,
        .inputs = (const char *[]){"seed.txt", nullptr},
        .outputs = (const char *[]){"word.txt", nullptr},
        .restat = true,
    };
    smith_step(b, &word);
    SmithStep copy = {
        .command = (const char *[]){"sh", "-c", "cp \"$0\" final.txt && echo x >> copies.log", "$in", nullptr},
        .inputs = word.outputs,
        .outputs = (const char *[]){"final.txt", nullptr},
    };
    smith_step(b, &copy);
    SmithStep task = {.run = show, .args = (const char *[]){"alpha", "beta", nullptr}};
    smith_task(b, "show", &task);
    SmithStep bad = {.run = fail_three, .outputs = (const char *[]){"bad.out", nullptr}};
    smith_step(b, &bad);
    SmithStep after = {
        .command = (const char *[]){"sh", "-c", "echo ran > after.out", nullptr},
        .inputs = bad.outputs,
        .outputs = (const char *[]){"after.out", nullptr},
    };
    smith_step(b, &after);
    smith_alias(b, "broken", after.outputs);
    smith_default(b, copy.outputs);
}
