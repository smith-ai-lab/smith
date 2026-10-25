#include <smith/build.h>
#include <stdio.h>
#include <string.h>

static bool in_list(const char *const *list, const char *string) {
    for (; *list; list++) {
        if (strcmp(*list, string) == 0) return true;
    }
    return false;
}

static int check_libc(SmithRun *run) {
    const char *const *ours = (const char *[]){nullptr};
    for (const char *const *object = run->inputs; *object; object++) {
        const char *const *defined = smith_run_lines(run, (const char *[]){"nm", "-gUj", *object, nullptr});
        if (defined == nullptr) return 1;
        ours = smith_cat(run->b, ours, defined, nullptr);
    }
    int failed = 0;
    for (const char *const *object = run->inputs; *object; object++) {
        const char *const *used = smith_run_lines(run, (const char *[]){"nm", "-uj", *object, nullptr});
        if (used == nullptr) return 1;
        for (const char *const *symbol = used; *symbol; symbol++) {
            const char *name = **symbol == '_' ? *symbol + 1 : *symbol;
            if (in_list(ours, *symbol) || in_list(run->args, name)) continue;
            fprintf(stderr, "%s uses %s from the C library, which is not allowed\n", *object, name);
            failed = 1;
        }
    }
    return failed;
}

void build(SmithBuild *b) {
    const char *const sources[] = {"base", "game", nullptr};
    const char *const *objects = (const char *[]){nullptr};
    for (const char *const *name = sources; *name; name++) {
        SmithStep cc = {
            .command = (const char *[]){"cc", "-O0", "-c", "$in", "-o", "$out", nullptr},
            .inputs = (const char *[]){smith_fmt(b, "%s.c", *name), nullptr},
            .outputs = (const char *[]){smith_fmt(b, "build/%s.o", *name), nullptr},
        };
        smith_step(b, &cc);
        objects = smith_cat(b, objects, cc.outputs, nullptr);
    }
    SmithStep check = {
        .run = check_libc,
        .args = (const char *[]){"memcpy", "memmove", "memset", "__memcpy_chk", "bzero", "sqrtf", "fabsf",
                                 "__stack_chk_guard", "__stack_chk_fail", nullptr},
        .inputs = objects,
        .description = "check-libc",
    };
    smith_step(b, &check);
    smith_alias(b, "check-libc", check.outputs);
    smith_default(b, check.outputs);
}
