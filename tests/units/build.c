// The units runner of smith's own tests (issue #31): every *_test.smith (beside the library it tests in lib/, or
// integration tests in tests/) linked into one runner, built once under AddressSanitizer and UBSan. build.c of the
// repository builds this as a project of its own, with the smith under test, so that the libraries it links and their
// headers come from that smith and not from the one that runs the repository's build.c (an older smith would not know a
// library added since). `tests` and `lib` there are links to the repository's.
#include <smith/build.h>
#include <stdlib.h>

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
    // lib/http.c is C and not a registered smith library (its only user is smith), so tests/http_test.smith reaches it
    // here, from the repository that build.c of the repository sets in SMITH_REPO.
    const char *repo = getenv("SMITH_REPO");
    SmithObject http = {
        .compiler = "smith-cc",
        .source = smith_fmt(b, "%s/lib/http.c", repo != nullptr ? repo : ".."),
        .output = "build/tests_objects/http.o",
        .flags = flags,
    };
    smith_object(b, &http);
    SmithTests tests = {
        .objects = (const SmithObject *[]){&http, nullptr},
        .sources = smith_glob(b, smith_list(b, "lib/smith/*_test.smith", "lib/*_test.smith", "tests/*_test.smith",
                                            "tests/scratch.smith", "tests/runner.smith", nullptr)),
        .flags = flags,
        .link_flags = smith_list(b, "-fsanitize=address,undefined", nullptr),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
