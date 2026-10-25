#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu2y", "-g", "-Wall", "-Wextra", "-Werror", "-I", ".smith/include", nullptr);
    SmithObject box = {.compiler = "smith-cc", .source = "src/geometry/box.smith", .output = "build/box.o", .flags = flags};
    smith_object(b, &box);
    SmithObject checks = {.compiler = "smith-cc", .source = "src/smith/test/helpers.smith", .output = "build/checks.o", .flags = flags};
    smith_object(b, &checks);
    SmithTests tests = {
        .sources = smith_glob(b, smith_list(b, "tests/test_*.c", "tests/test_*.smith", "tests/**/*_test.smith", nullptr)),
        .objects = (const SmithObject *[]){&box, &checks, nullptr},
        .flags = flags,
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
