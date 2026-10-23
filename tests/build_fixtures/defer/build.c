#include <smith/build.h>
#include <stddef.h>

static const char *suffix = "";

static void more(SmithBuild *b) {
    const char *const *names = smith_lines(b, "list");
    for (size_t i = 0; names[i] != nullptr; i++) {
        const char *out = smith_fmt(b, "%s.out", names[i]);
        SmithStep step = {
            .command = (const char *[]){"sh", "-c", "echo \"$0$2\" > \"$1\"; echo \"$0\" >> ran", names[i], out, suffix,
                                        nullptr},
            .outputs = (const char *[]){out, nullptr},
        };
        smith_step(b, &step);
        smith_default(b, step.outputs);
    }
}

void build(SmithBuild *b) {
    suffix = smith_option_value(b, "suffix", "", "text added to every output");
    SmithStep list = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = (const char *[]){"source", nullptr},
        .outputs = (const char *[]){"list", nullptr},
    };
    smith_step(b, &list);
    smith_defer(b, list.outputs, more);
}
