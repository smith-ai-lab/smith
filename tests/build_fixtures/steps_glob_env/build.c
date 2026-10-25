#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep join = {
        .command = (const char *[]){"sh", "-c", "cat \"$@\" > joined", "x", "$in", nullptr},
        .inputs = smith_glob(b, (const char *[]){"src/*.txt", nullptr}),
        .outputs = (const char *[]){"joined", nullptr},
    };
    smith_step(b, &join);
    SmithStep env = {
        .command = (const char *[]){"sh", "-c", "echo \"$0\" > \"$1\"", smith_env(b, "SMITH_TEST_VALUE", "unset"), "$out", nullptr},
        .outputs = (const char *[]){"env.out", nullptr},
    };
    smith_step(b, &env);
    SmithStep captured = {
        .command = (const char *[]){"sh", "-c", "echo \"$0\" > \"$1\"", smith_capture(b, (const char *[]){"echo", "from capture", nullptr}), "$out", nullptr},
        .outputs = (const char *[]){"capture.out", nullptr},
    };
    smith_step(b, &captured);
    smith_default(b, smith_cat(b, join.outputs, env.outputs, captured.outputs, nullptr));
}
