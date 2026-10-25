#include <smith/build.h>

static void more(SmithBuild *b) {
    const char *suffix = smith_option_value(b, "suffix", "none", "the word written to out");
    SmithStep out = {
        .command = (const char *[]){"sh", "-c", "echo \"$0\" > \"$1\"", suffix, "$out", nullptr},
        .inputs = (const char *[]){"list", nullptr},
        .outputs = (const char *[]){"out", nullptr},
    };
    smith_step(b, &out);
    smith_default(b, out.outputs);
}

void build(SmithBuild *b) {
    SmithStep list = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = (const char *[]){"source", nullptr},
        .outputs = (const char *[]){"list", nullptr},
    };
    smith_step(b, &list);
    smith_defer(b, list.outputs, more);
}
