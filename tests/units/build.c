// The units runner of smith's own tests (issue #31): every *_test.smith (beside the library it tests in lib/, or
// integration tests in tests/) linked into one runner, built once under AddressSanitizer and UBSan. build.c of the
// repository builds this as a project of its own, with the smith under test, so that the libraries it links and their
// headers come from that smith and not from the one that runs the repository's build.c (an older smith would not know a
// library added since). `tests` and `lib` there are links to the repository's.
#include <smith/build.h>
#include <stdlib.h>

void build(SmithBuild *b) {
    // The repository that build.c of the repository sets in SMITH_REPO.
    const char *repo = getenv("SMITH_REPO");
    const char *root = repo != nullptr ? repo : "..";
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
                                 "-I",
                                 smith_fmt(b, "%s/include", root),
                                 nullptr};
    // lib/http.c is C and not a registered smith library (its only user is smith), so tests/http_test.smith reaches it
    // here.
    SmithObject http = {
        .compiler = "smith-cc",
        .source = smith_fmt(b, "%s/lib/http.c", root),
        .output = "build/tests_objects/http.o",
        .flags = flags,
    };
    smith_object(b, &http);
    // smith's own core (issue #646): smith.c compiled with -DSMITH_CORE has no main, so the tests call the runtime in
    // process (include/smith_core.h, which no library ships). It is built under smith's own
    // flags (gnu23), not the tests' stricter ones, and sees the libraries as their headers, as smith does.
    SmithObject core = {
        .compiler = "smith-cc",
        .source = smith_fmt(b, "%s/smith.c", root),
        .output = "build/tests_objects/core.o",
        .flags = smith_list(b, "-std=gnu23", "-g", "-O0", "-Wall", "-Wextra", "-Wpedantic", "-Wno-unused-function",
                            "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-DSMITH_CORE",
                            "-DSMITH_CLANG_ID=\"core\"", "-I", smith_fmt(b, "%s/include", root), "-I", ".smith/include",
                            "-I", smith_fmt(b, "%s/runtime/protocol", root), nullptr),
    };
    smith_object(b, &core);
    SmithTests tests = {
        .objects = (const SmithObject *[]){&http, &core, nullptr},
        .sources =
            smith_glob(b, smith_list(b, "lib/std/*_test.smith", "lib/*_test.smith", "tests/*_test.smith",
                                     "tests/scratch.smith", "tests/runner.smith", "tests/prove_cases.smith", nullptr)),
        .flags = flags,
        .link_flags = smith_list(b, "-fsanitize=address,undefined", nullptr),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
