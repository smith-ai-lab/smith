#include <smith/build.h>

#include <string.h>

void build(SmithBuild *b) {
    bool sanitize = strcmp(smith_option_value(b, "SAN", "1", "0 builds without the sanitizers"), "1") == 0;
    const char *const *flags = smith_cat(b, smith_list(b, "-std=gnu2y", "-g", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-I", ".smith/include", nullptr),
                                         smith_when(b, sanitize, smith_list(b, "-fsanitize=address,undefined", "-fno-sanitize-recover=all", nullptr)), nullptr);
    SmithObject arena = {.compiler = "smith-cc", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
    smith_object(b, &arena);
    SmithObject random = {.compiler = "smith-cc", .source = smith_library(b, "random"), .output = "build/smith/random.o", .flags = flags};
    smith_object(b, &random);
    SmithObject contract = {.compiler = "smith-cc", .source = smith_library(b, "contract"), .output = "build/smith/contract.o", .flags = flags};
    smith_object(b, &contract);
    SmithTests tests = {
        .sources = smith_glob(b, smith_list(b, "tests/test_*.c", nullptr)),
        .objects = (const SmithObject *[]){&arena, &random, &contract, nullptr},
        .flags = flags,
        .link_flags = smith_when(b, sanitize, smith_list(b, "-fsanitize=address,undefined", nullptr)),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
