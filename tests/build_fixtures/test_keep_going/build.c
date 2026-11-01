// `smith test` keeps going (#1171): `fail` is a test task that fails at once; `ok` is a test task whose runner is built
// by `prep`, which waits behind `gate` until `fail` has failed. Stopping at the first failure never starts `prep`, so
// the runner `prep` stands for stays old.
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep fail = {.command = (const char *[]){"sh", "-c", "touch failed; exit 1", nullptr}, .test = true};
    smith_task(b, "fail", &fail);
    SmithStep gate = {
        .command = (const char *[]){"sh", "-c",
                                    "n=0; while [ ! -e failed ] && [ $n -lt 100 ]; do sleep 0.1; n=$((n+1)); done",
                                    nullptr},
    };
    smith_task(b, "gate", &gate);
    SmithStep prep = {.command = (const char *[]){"sh", "-c", "touch prepared", nullptr},
                      .inputs = (const char *[]){"gate", nullptr}};
    smith_task(b, "prep", &prep);
    SmithStep ok = {.command = (const char *[]){"sh", "-c", "true", nullptr},
                    .inputs = (const char *[]){"prep", nullptr},
                    .test = true};
    smith_task(b, "ok", &ok);
    smith_default(b, smith_list(b, "fail", nullptr));
}
