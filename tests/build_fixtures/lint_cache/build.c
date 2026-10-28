#include <smith/build.h>

void build(SmithBuild *b) {
    SmithObject a = {.compiler = "smith-cc", .source = "a.c", .output = "build/a.o"};
    SmithObject second = {.compiler = "smith-cc", .source = "b.c", .output = "build/b.o"};
    SmithObject third = {.compiler = "smith-cc", .source = "c.c", .output = "build/c.o"};
    smith_object(b, &a);
    smith_object(b, &second);
    smith_object(b, &third);
    smith_default(b, smith_list(b, "build/a.o", "build/b.o", "build/c.o", nullptr));
}
