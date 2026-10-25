#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu2y", "-g", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-I", ".smith/include", nullptr);
    SmithObject arena = {.compiler = "smith-cc", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
    smith_object(b, &arena);
    SmithObject random = {.compiler = "smith-cc", .source = smith_library(b, "random"), .output = "build/smith/random.o", .flags = flags};
    smith_object(b, &random);
    SmithStep data = {
        .command = smith_list(b, "cp", "gen.txt", "$out", nullptr),
        .implicit_inputs = smith_list(b, "gen.txt", nullptr),
        .outputs = smith_list(b, "build/data.txt", nullptr),
        .description = "generate the data",
    };
    smith_step(b, &data);
    SmithTests tests = {
        .sources = smith_list(b, "tests/test_extras.smith", "tests/test_extras_c.c", nullptr),
        .objects = (const SmithObject *[]){&arena, &random, nullptr},
        .flags = flags,
        .inputs = data.outputs,
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
