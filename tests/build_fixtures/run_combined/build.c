#include <smith/build.h>
#include <stdio.h>

static int probe(SmithRun *run) {
    const char *const *lines = nullptr;
    FILE *out = fopen("combined.txt", "w");
    int status = smith_run_combined(
        run, (const char *[]){"sh", "-c", "echo o1; echo e1 >&2; echo o2; echo e2 >&2; echo o3; exit 4", nullptr},
        &lines);
    fprintf(out, "sh %d", status);
    for (const char *const *line = lines; *line; line++)
        fprintf(out, " [%s]", *line);
    fputc('\n', out);
    status = smith_run_combined(run, (const char *[]){"sh", "-c", "echo before; kill -9 $$", nullptr}, &lines);
    fprintf(out, "killed %d %s\n", status, lines != nullptr && lines[0] != nullptr ? lines[0] : "none");
    status = smith_run_combined(run, (const char *[]){"smith-no-such-program", nullptr}, &lines);
    fprintf(out, "missing %d lines %s\n", status, lines == nullptr ? "null" : "set");
    status = smith_run_combined(run, (const char *[]){"true", nullptr}, nullptr);
    fprintf(out, "true %d\n", status);
    fprintf(out, "status %d %d\n", smith_run_status(run, (const char *[]){"sh", "-c", "exit 4", nullptr}, nullptr),
            smith_run_status(run, (const char *[]){"sh", "-c", "kill -9 $$", nullptr}, nullptr));
    fclose(out);
    return 0;
}

void build(SmithBuild *b) {
    SmithStep step = {.run = probe, .outputs = (const char *[]){"combined.txt", nullptr}};
    smith_step(b, &step);
    smith_default(b, step.outputs);
}
