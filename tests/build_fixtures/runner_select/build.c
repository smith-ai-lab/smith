#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu2y", "-g", "-Wall", "-Wextra", "-Werror", "-I", ".smith/include", nullptr);
    SmithTests tests = {
        .sources = smith_glob(b, smith_list(b, "tests/**/*_test.smith", nullptr)),
        .flags = flags,
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
