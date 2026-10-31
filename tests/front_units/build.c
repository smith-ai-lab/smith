// The front end's test runner (issue #1115, track B of #1112): every front/*.smith and every tests/front_*_test.smith
// linked into one runner, with LLVM's C API. It is a second runner beside the units runner (tests/units/build.c), which
// would be relinked against static LLVM on every test change. build.c of the repository builds this as a project of its
// own with the smith under test (`<folder>/front-project`; `front`, `tests` and `lib` there are links to the
// repository's) and sets SMITH_REPO and SMITH_LLVM: the LLVM install that smith itself links.
#include <smith/build.h>
#include <stdlib.h>

void build(SmithBuild *b) {
    const char *repo = getenv("SMITH_REPO");
    const char *root = repo != nullptr ? repo : "..";
    const char *llvm = getenv("SMITH_LLVM");
    if (llvm == nullptr) llvm = "";
    const char *const flags[] = {"-std=gnu2y", "-g", "-O0", "-Wall", "-Wextra", "-Wpedantic", "-Wshadow",
                                 "-Wconversion", "-Werror", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                                 "-I", ".smith/include", "-I", smith_fmt(b, "%s/include", root),
                                 // -I and not -isystem: smith-ownership asks libc's table (shim/effects_table.h) for
                                 // the idiom of every pointer a system header's function returns, and LLVM's handles
                                 // are not libc's.
                                 "-I", smith_fmt(b, "%s/include", llvm), nullptr};
    // LLVM's static archives name each other: on Linux they are linked in a group, and what the C API does not reach is
    // dropped (build.c of the repository does the same for smith's own link).
    const char *const *archives = smith_glob(b, smith_list(b, smith_fmt(b, "%s/lib/libLLVM*.a", llvm), nullptr));
#ifdef __APPLE__
    const char *const *libraries =
        smith_cat(b, archives, smith_list(b, "-lc++", "-lm", "-lcompression", nullptr), nullptr);
    const char *const *gc = smith_list(b, "-Wl,-dead_strip", nullptr);
#else
    const char *const *libraries =
        smith_cat(b, smith_list(b, "-Wl,--start-group", nullptr), archives,
                  smith_list(b, "-Wl,--end-group", "-lstdc++", "-lm", "-lpthread", "-ldl", "-lrt", nullptr), nullptr);
    const char *const *gc = smith_list(b, "-Wl,--gc-sections", nullptr);
#endif
    SmithTests tests = {
        .sources = smith_glob(b, smith_list(b, "front/*.smith", "tests/front_*_test.smith", nullptr)),
        .flags = flags,
        // LLVM keeps process-lifetime tables (its pass registry, the targets) that the first call allocates and nothing
        // frees, which the per-test leak check of mem::checked would blame on the first test. ASan's leak check stays
        // on.
        .checked_allocator = SmithSetting_Off,
        .link_flags = smith_cat(b, smith_list(b, "-fsanitize=address,undefined", nullptr), gc, libraries, nullptr),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
