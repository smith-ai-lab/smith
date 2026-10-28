#include <smith/build.h>

// a.c includes a header a step makes, c.c includes nothing: `smith lint --changed` builds the header only when it lints
// a.c.
void build(SmithBuild *b) {
    SmithStep generate = {.command = (const char *[]){"sh", "-c", "echo 'int generated(void);' > $0", "build/gen.h", nullptr},
                          .outputs = (const char *[]){"build/gen.h", nullptr},
                          .description = "make gen.h"};
    smith_step(b, &generate);
    const char *const flags[] = {"-I", "build", nullptr};
    SmithObject a = {.compiler = "smith-cc", .source = "a.c", .output = "build/a.o", .flags = flags,
                     .implicit_inputs = generate.outputs};
    SmithObject third = {.compiler = "smith-cc", .source = "c.c", .output = "build/c.o"};
    smith_object(b, &a);
    smith_object(b, &third);
    smith_default(b, smith_list(b, "build/a.o", "build/c.o", nullptr));
}
