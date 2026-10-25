#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep tick = {
        .command = (const char *[]){"sh", "-c", "echo x >> ticks; touch \"$0\"", "$out", nullptr},
        .outputs = (const char *[]){"tick", nullptr},
        .always = true,
    };
    smith_step(b, &tick);
    smith_default(b, tick.outputs);
}
