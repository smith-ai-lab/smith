#include <smith/build.h>

void build(SmithBuild *b) {
    SmithTests tests = {
        .sources = smith_list(b, "tests/test_a.c", nullptr),
        .flags = smith_list(b, "-std=gnu23", "-g", "-O0", "-I", ".smith/include", nullptr),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
