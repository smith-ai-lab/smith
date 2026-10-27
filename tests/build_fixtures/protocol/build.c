#include <smith/build.h>

#include <string.h>

void build(SmithBuild *b) {
    const char *mode = smith_option_value(b, "MODE", "asan", "asan, fast or ndebug");
    bool sanitize = strcmp(mode, "asan") == 0;
    const char *const *flags = smith_cat(
        b, smith_list(b, "-std=gnu23", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Wshadow", "-Wconversion", "-Werror", "-I", ".smith/include", "-I", "protocol", nullptr),
        sanitize ? smith_list(b, "-O0", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", nullptr)
                 : smith_list(b, "-O2", nullptr),
        strcmp(mode, "ndebug") == 0 ? smith_list(b, "-DNDEBUG", nullptr) : smith_list(b, nullptr), nullptr);
    const char *const libraries[] = {"arena", "random", "format", "json", nullptr};
    const char *const sources[] = {"protocol/protocol.c", "protocol/protocol_support.c", "protocol/dap.c", "protocol/mcp.c", nullptr};
    SmithObject objects[8] = {};
    size_t count = 0;
    for (size_t i = 0; libraries[i] != nullptr; i++, count++) {
        objects[count] = (SmithObject){.compiler = "smith-cc", .source = smith_library(b, libraries[i]),
                                       .output = smith_fmt(b, "build/smith/%s.o", libraries[i]), .flags = flags};
        smith_object(b, &objects[count]);
    }
    for (size_t i = 0; sources[i] != nullptr; i++, count++) {
        objects[count] = (SmithObject){.compiler = "smith-cc", .source = sources[i],
                                       .output = smith_fmt(b, "build/protocol/%zu.o", i), .flags = flags};
        smith_object(b, &objects[count]);
    }
    const SmithObject *linked[9] = {};
    for (size_t i = 0; i < count; i++) linked[i] = &objects[i];
    SmithTests tests = {
        .sources = smith_glob(b, smith_list(b, "tests/test_*.c", nullptr)),
        .objects = linked,
        .flags = flags,
        .link_flags = smith_when(b, sanitize, smith_list(b, "-fsanitize=address,undefined", nullptr)),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
