#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep first = {
        .command = (const char *[]){"sh", "-c", "echo ran >> log", nullptr},
        .inputs = (const char *[]){"one", nullptr},
    };
    smith_step(b, &first);
    SmithStep second = {
        .command = (const char *[]){"sh", "-c", "echo ran >> log", nullptr},
        .inputs = (const char *[]){"two", nullptr},
    };
    smith_step(b, &second);
    smith_default(b, smith_cat(b, first.outputs, second.outputs, nullptr));
}
