#include <smith/build.h>

void build(SmithBuild *b) {
    SmithObject app = {.compiler = smith_option_value(b, "CC", smith_env(b, "CC", "smith-cc"), "the compiler"),
                       .source = "main.c", .output = "main.o"};
    smith_object(b, &app);
    smith_default(b, app.step.outputs);
}
