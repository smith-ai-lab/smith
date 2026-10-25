#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep object = {
        .command = (const char *[]){"sh", "-c", "cc -MMD -MF a.o.d -c a.c -o a.o && echo x >> count", nullptr},
        .inputs = (const char *[]){"a.c", nullptr},
        .outputs = (const char *[]){"a.o", nullptr},
        .depfile = "a.o.d",
    };
    smith_step(b, &object);
    smith_default(b, object.outputs);
}
