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
    // Track C (front/cimport) reads headers through clang's libraries, so the group holds those too.
    const char *const *archives = smith_glob(b, smith_list(b, smith_fmt(b, "%s/lib/libclang[A-Z]*.a", llvm),
                                                           smith_fmt(b, "%s/lib/libLLVM*.a", llvm), nullptr));
#ifdef __APPLE__
    const char *const *libraries =
        smith_cat(b, archives, smith_list(b, "-lc++", "-lm", "-lcompression", nullptr), nullptr);
    // No -dead_strip here: it drops smith_asan_options.o (a weak __asan_default_options that nothing references, which
    // every sanitized program links on macOS, knowledge/handlers.md), and ASan then aborts at start-up with
    // "interceptors not installed". Static archives link only the objects the runner reaches anyway.
    const char *const *gc = smith_list(b, nullptr);
#else
    const char *const *libraries =
        smith_cat(b, smith_list(b, "-Wl,--start-group", nullptr), archives,
                  smith_list(b, "-Wl,--end-group", "-lstdc++", "-lm", "-lpthread", "-ldl", "-lrt", nullptr), nullptr);
    const char *const *gc = smith_list(b, "-Wl,--gc-sections", nullptr);
#endif
    // The edge files to LLVM (shim/cimport*.cpp, none yet) are C++ and compiled with build.c's shim flags under the
    // runner's sanitizers (-fno-rtti, so no vptr check) and linked in. An empty glob links nothing.
    const char *const *cxx_sources = smith_glob(b, smith_list(b, smith_fmt(b, "%s/shim/cimport*.cpp", root), nullptr));
    const char *const *cxx_flags =
        smith_list(b, "-std=c++17", "-g", "-O0", "-Wall", "-Wextra", "-Werror", "-fno-exceptions", "-fno-rtti",
                   "-D__STDC_CONSTANT_MACROS", "-D__STDC_FORMAT_MACROS", "-D__STDC_LIMIT_MACROS", "-isystem",
                   smith_fmt(b, "%s/include", llvm), "-fsanitize=address,undefined", "-fno-sanitize=vptr",
                   "-fno-sanitize-recover=all", nullptr);
    static SmithObject objects[64];
    const SmithObject *cxx_objects[64];
    size_t cxx_count = 0;
    for (size_t i = 0; cxx_sources[i] != nullptr && cxx_count < 63; i++) {
        SmithObject *object = &objects[cxx_count];
        *object = (SmithObject){
            .compiler = smith_fmt(b, "%s/bin/clang++", llvm),
            .source = cxx_sources[i],
            .output = smith_fmt(b, "build/tests_objects/cimport_%zu.o", i),
            .flags = cxx_flags,
        };
        smith_object(b, object);
        cxx_objects[cxx_count++] = object;
    }
    cxx_objects[cxx_count] = nullptr;
    SmithTests tests = {
        .objects = cxx_objects,
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
