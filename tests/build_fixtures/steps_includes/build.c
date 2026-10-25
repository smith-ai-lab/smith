#include "config.h"
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep write = {
        .command = (const char *[]){"sh", "-c", "echo \"$0\" > out", MESSAGE, nullptr},
        .outputs = (const char *[]){"out", nullptr},
    };
    smith_step(b, &write);
    smith_default(b, write.outputs);
}
