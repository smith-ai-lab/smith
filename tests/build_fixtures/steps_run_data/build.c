#include <smith/build.h>
#include <stdio.h>

static int show(SmithRun *run) {
    FILE *out = fopen(run->outputs[0], "w");
    for (const char *const *input = run->inputs; *input; input++)
        fprintf(out, "input [%s]\n", *input);
    for (const char *const *output = run->outputs; *output; output++)
        fprintf(out, "output [%s]\n", *output);
    for (const char *const *arg = run->args; *arg; arg++)
        fprintf(out, "arg [%s]\n", *arg);
    fprintf(out, "who [%s]\n", smith_option_value(run->b, "who", "unset", nullptr));
    fclose(out);
    return 0;
}

void build(SmithBuild *b) {
    FILE *count = fopen("calls.log", "a");
    fputs("build\n", count);
    fclose(count);
    smith_option_value(b, "who", "nobody", "a name");
    SmithStep step = {
        .run = show,
        .inputs = (const char *[]){"in put.txt", "$HOME.txt", nullptr},
        .outputs = (const char *[]){"seen.txt", nullptr},
        .args = (const char *[]){"two words", "$dollar", "it's", "--arg", nullptr},
    };
    smith_step(b, &step);
    SmithStep other = {.run = show, .inputs = (const char *[]){"in put.txt", nullptr}, .outputs = (const char *[]){"other.out", nullptr}};
    smith_step(b, &other);
    smith_default(b, smith_cat(b, step.outputs, other.outputs, nullptr));
}
