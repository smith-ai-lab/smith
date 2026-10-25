#include <smith/build.h>

void build(SmithBuild *b) {
    smith_modules(b, &(SmithModules){.folders = (const char *[]){"inc", nullptr}, .textual = smith_list(b, smith_option_value(b, "TEXTUAL", nullptr, "a header"), nullptr)});
    const char *source = smith_option_value(b, "SOURCE", "bad.c", "the source");
    SmithObject o = {.compiler = "cc", .source = source, .output = "build/o.o", .flags = smith_list(b, "-std=gnu23", nullptr)};
    smith_object(b, &o);
    smith_default(b, o.step.outputs);
}
