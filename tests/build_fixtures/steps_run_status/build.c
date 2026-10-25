#include <smith/build.h>
#include <stdio.h>

static int probe(SmithRun *run) {
    const char *const *lines = nullptr;
    FILE *out = fopen("status.txt", "w");
    fprintf(out, "true %d\n", smith_run_status(run, (const char *[]){"true", nullptr}, nullptr));
    fprintf(out, "false %d\n", smith_run_status(run, (const char *[]){"false", nullptr}, &lines));
    fprintf(out, "missing %d lines %s\n", smith_run_status(run, (const char *[]){"smith-no-such-program", nullptr}, &lines),
            lines == nullptr ? "null" : "set");
    int status = smith_run_status(run, (const char *[]){"sh", "-c", "printf 'a\n\nb c\nlast'; exit 4", nullptr}, &lines);
    fprintf(out, "sh %d", status);
    for (const char *const *line = lines; *line; line++)
        fprintf(out, " [%s]", *line);
    fputc('\n', out);
    const char *const *same = smith_run_lines(run, (const char *[]){"printf", "x\ny\n", nullptr});
    fprintf(out, "lines %s %s %s\n", same[0], same[1], same[2] == nullptr ? "end" : "more");
    fprintf(out, "failing %s\n", smith_run_lines(run, (const char *[]){"sh", "-c", "echo oops >&2; exit 5", nullptr}) == nullptr ? "null" : "set");
    fclose(out);
    return 0;
}

void build(SmithBuild *b) {
    SmithStep step = {.run = probe, .outputs = (const char *[]){"status.txt", nullptr}};
    smith_step(b, &step);
    smith_default(b, step.outputs);
}
