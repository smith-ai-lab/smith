// The units runner of smith's own tests (issue #31): every tests/*.smith linked into one runner, built once under
// AddressSanitizer and UBSan. build.c of the repository builds this as a project of its own, with the smith under test,
// so that the libraries it links and their headers come from that smith and not from the one that runs the repository's
// build.c (an older smith would not know a library added since). `tests` there is a link to the repository's.
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const flags[] = {"-std=gnu2y",
                                 "-g",
                                 "-O0",
                                 "-Wall",
                                 "-Wextra",
                                 "-Wpedantic",
                                 "-Wshadow",
                                 "-Wconversion",
                                 "-Werror",
                                 "-fsanitize=address,undefined",
                                 "-fno-sanitize-recover=all",
                                 "-I",
                                 ".smith/include",
                                 nullptr};
    SmithTests tests = {
        .sources = smith_glob(b, smith_list(b, "tests/*.smith", nullptr)),
        .flags = flags,
        .link_flags = smith_list(b, "-fsanitize=address,undefined", nullptr),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
