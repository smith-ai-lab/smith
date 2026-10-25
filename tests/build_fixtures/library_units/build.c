// A library's test file built as a project's tests with smith_tests (smith's own test library), in a mode: -DMODE is
// asan (sanitized, -O0), fast (-O2) or ndebug (-O2 -DNDEBUG); -DLIBS names the libraries, comma separated, and -DTEST
// the test file in the project.
#include <smith/build.h>

#include <string.h>

void build(SmithBuild *b) {
    const char *mode = smith_option_value(b, "MODE", "asan", "asan, fast or ndebug");
    const char *libs = smith_option_value(b, "LIBS", "arena", "the libraries, comma separated");
    const char *test = smith_option_value(b, "TEST", "test.smith", "the test file");
    bool sanitize = strcmp(mode, "asan") == 0;
    const char *const *flags = smith_cat(
        b, smith_list(b, "-std=gnu2y", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Wshadow", "-Wconversion", "-Werror", "-I", ".smith/include", nullptr),
        sanitize ? smith_list(b, "-O0", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", nullptr)
                 : smith_list(b, "-O2", nullptr),
        strcmp(mode, "ndebug") == 0 ? smith_list(b, "-DNDEBUG", nullptr) : smith_list(b, nullptr), nullptr);
    SmithObject objects[8] = {};
    const SmithObject *pointers[9] = {};
    size_t count = 0;
    char *rest = (char *)smith_fmt(b, "%s", libs); // a copy for strsep to cut
    for (char *name = strsep(&rest, ","); name != nullptr && count < 8; name = strsep(&rest, ",")) {
        objects[count] = (SmithObject){.compiler = "smith-cc", .source = smith_library(b, name),
                                       .output = smith_fmt(b, "build/smith/%s.o", name), .flags = flags};
        smith_object(b, &objects[count]);
        pointers[count] = &objects[count];
        count++;
    }
    SmithTests tests = {
        .sources = smith_list(b, test, nullptr),
        .objects = pointers,
        .flags = flags,
        .link_flags = smith_when(b, sanitize, smith_list(b, "-fsanitize=address,undefined", nullptr)),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
