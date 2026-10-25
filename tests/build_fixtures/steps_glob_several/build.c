#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep join = {
        .command = (const char *[]){"sh", "-c", "printf '%s\\n' \"$@\" > joined", "x", "$in", nullptr},
        .inputs = smith_glob(b, (const char *[]){"src/*.txt", "src/a*", "other/*.md", "none*", nullptr}),
        .outputs = (const char *[]){"joined", nullptr},
    };
    smith_step(b, &join);
    smith_default(b, join.outputs);
}
