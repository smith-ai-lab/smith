// The units runner of smith's own tests (issue #31): every *_test.smith (beside the library it tests in lib/, or
// integration tests in tests/) linked into one runner, built once under AddressSanitizer and UBSan. build.c of the
// repository builds this as a project of its own, with the smith under test, so that the libraries it links and their
// headers come from that smith and not from the one that runs the repository's build.c (an older smith would not know a
// library added since). `tests` and `lib` there are links to the repository's.
#include <smith/build.h>
#include <stdlib.h>
#include <string.h>

// The build program's runtime (runtime/build_runtime.c, issue #1300) compiled with -DSMITH_BUILD_CORE has no main and
// no build file to run: the tests reach its refusals in process (include/smith_core_verbs.h).
static SmithObject build_core_object(SmithBuild *b, const char *root) {
    SmithObject object = {
        .compiler = "smith-cc",
        .source = smith_fmt(b, "%s/runtime/build_runtime.c", root),
        .output = "build/tests_objects/build_core.o",
        .flags = smith_list(b, "-std=gnu23", "-g", "-O0", "-Wall", "-Wextra", "-Wpedantic", "-Wno-unused-function",
                            "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-DSMITH_BUILD_CORE", "-I",
                            smith_fmt(b, "%s/include", root), "-I", ".smith/include", nullptr),
    };
    smith_object(b, &object);
    return object;
}

// The runtime files that smith.c used to include (#2685), each compiled as smith's core with the flags of smith.c: all
// of runtime/*.c but what a program does not link in the units runner (the build program's files, the embedded data,
// the mcp family that -DSMITH_MCP turns on, the macOS ones, and package.c, which package_unit.c wraps).
static const char *const core_skipped[] = {
    "build_runtime.c", "commit.c",  "gzip.c",     "lint_identity.c", "mcp.c",         "mcp_dap.c",
    "mcp_serve.c",     "mcp_show.c", "mcp_text.c", "mcp_tools.c",     "package.c",     "pack_data.c",
    "platform_macos.c", "sdk_cache.c", "tree_sitter_data.c", "untar.c",     "xz.c",
    nullptr,
};

static bool core_is_skipped(const char *path) {
    const char *name = strrchr(path, '/');
    name = name != nullptr ? name + 1 : path;
    for (size_t i = 0; core_skipped[i] != nullptr; i++) // bounded by the table
        if (strcmp(name, core_skipped[i]) == 0) return true;
    return false;
}

static const SmithObject *const *core_runtime_objects(SmithBuild *b, const char *root, const char *const *flags,
                                                      const SmithObject *const *first, size_t first_count) {
    static SmithObject objects[128];
    static const SmithObject *list[140];
    size_t count = 0;
    for (; count < first_count; count++) // bounded by the caller's few
        list[count] = first[count];
    size_t made = 0;
    const char *const *sources = smith_glob(b, smith_list(b, smith_fmt(b, "%s/runtime/*.c", root), nullptr));
    for (size_t i = 0; sources[i] != nullptr && made < 128; i++) { // bounded by the tree
        if (core_is_skipped(sources[i])) continue;
        const char *name = strrchr(sources[i], '/') + 1;
        objects[made] = (SmithObject){
            .compiler = "smith-cc",
            .source = sources[i],
            .output = smith_fmt(b, "build/tests_objects/rt_%s.o", name),
            .flags = flags,
        };
        smith_object(b, &objects[made]);
        list[count++] = &objects[made++];
    }
    list[count] = nullptr;
    return list;
}

// The integration tests of tests/ that this runner links: all but the front end's.
static const char *const *tests_without_front(SmithBuild *b) {
    const char *const *all_tests = smith_glob(b, smith_list(b, "tests/*_test.smith", nullptr));
    static const char *kept[512];
    size_t kept_count = 0;
    for (size_t i = 0; all_tests[i] != nullptr && kept_count < 511; i++)
        if (strncmp(all_tests[i], "tests/front_", 12) != 0) kept[kept_count++] = all_tests[i];
    kept[kept_count] = nullptr;
    return kept;
}

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
    const char *const *core_flags =
        smith_list(b, "-std=gnu23", "-g", "-O0", "-Wall", "-Wextra", "-Wpedantic", "-Wno-unused-function",
                   "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-DSMITH_CORE",
                   "-DSMITH_CLANG_ID=\"core\"", "-I", smith_fmt(b, "%s/include", root), "-I", ".smith/include", "-I",
                   smith_fmt(b, "%s/runtime/protocol", root), nullptr);
    SmithObject core = {
        .compiler = "smith-cc",
        .source = smith_fmt(b, "%s/smith.c", root),
        .output = "build/tests_objects/core.o",
        .flags = core_flags,
    };
    smith_object(b, &core);
    SmithObject build_core = build_core_object(b, root);
    // smith's private io layer (issue #903): smith.c includes its header, which this object's step writes, and the core
    // calls it, so the object is linked.
    SmithObject runtime_io = {
        .compiler = "smith-cc",
        .source = smith_fmt(b, "%s/runtime/io.smith", root),
        .output = "build/tests_objects/runtime_io.o",
        .flags = flags,
    };
    smith_object(b, &runtime_io);
    // The front end's tests (tests/front_*_test.smith) link LLVM, so they have a runner of their own
    // (tests/front_units/build.c): this one leaves them out.
    const char *const *kept = tests_without_front(b);
    SmithTests tests = {
        .objects = core_runtime_objects(b, root, core_flags, (const SmithObject *[]){&http, &core, &build_core, &runtime_io}, 4),
        .sources = smith_cat(b, smith_glob(b, smith_list(b, "lib/std/*_test.smith", "lib/*_test.smith", nullptr)), kept,
                             smith_list(b, "tests/scratch.smith", "tests/runner.smith", "tests/prove_cases.smith",
                                        "tests/corpus.smith", nullptr),
                             nullptr),
        .flags = flags,
        .link_flags = smith_list(b, "-fsanitize=address,undefined", nullptr),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
