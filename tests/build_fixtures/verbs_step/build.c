#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep step = {.command = (const char *[]){"sh", "-c", "echo ran >> runs; cp in.txt $0", "build/checked", nullptr},
                      .inputs = (const char *[]){"in.txt", nullptr}, .outputs = (const char *[]){"build/checked", nullptr},
                      .test = true};
    smith_step(b, &step);
    smith_default(b, smith_list(b, nullptr));
}
