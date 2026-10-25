#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu2y", "-I", ".smith/include", nullptr);


    SmithTests tests = {
        .sources = smith_list(b, "tests/test_x.smith", nullptr),
        .flags = flags,
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
