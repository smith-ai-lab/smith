#include <smith/build.h>

#include <stddef.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu2y", "-g", "-O0", "-Wall", "-Wextra", "-Wpedantic", "-Wshadow", "-Wconversion", "-Werror", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-I", ".smith/include", nullptr);
    const char *const *names = smith_list(b, "arena", "contract", "random", nullptr);
    SmithObject objects[3] = {};
    for (size_t i = 0; names[i] != nullptr; i++) {
        objects[i] = (SmithObject){.compiler = "smith-cc", .source = smith_library(b, names[i]),
                                   .output = smith_fmt(b, "build/smith/%s.o", names[i]), .flags = flags};
        smith_object(b, &objects[i]);
    }
    SmithTests tests = {
        .sources = smith_list(b, "smith/contract_test.smith", nullptr),
        .objects = (const SmithObject *[]){&objects[0], &objects[1], &objects[2], nullptr},
        .flags = flags,
        .link_flags = smith_list(b, "-fsanitize=address,undefined", nullptr),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
