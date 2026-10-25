#include <smith/build.h>
#include <stdlib.h>

static void more(SmithBuild *b) {
    const char *name = smith_option_value(b, "name", nullptr, nullptr);
    bool loud = smith_option(b, "loud", nullptr);
    if (getenv("SMITH_TEST_BAD") != nullptr) smith_option(b, "name", nullptr);
    SmithStep step = {
        .command = (const char *[]){"sh", "-c", "echo \"$0 $1\" > \"$2\"", name, loud ? "loud" : "quiet", "$out", nullptr},
        .outputs = (const char *[]){"greeting", nullptr},
    };
    smith_step(b, &step);
    smith_default(b, step.outputs);
}

void build(SmithBuild *b) {
    smith_option_value(b, "name", "world", "who to greet");
    smith_option(b, "loud", "print loudly");
    smith_defer(b, (const char *[]){"source", nullptr}, more);
}
