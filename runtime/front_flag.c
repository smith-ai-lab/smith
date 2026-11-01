// `smith cc --front-end=new` (issue #1221, J2 of #1112): the `.smith` files of the command line go through smith's own
// front end (front/compile.smith: parse, check, emit, write) and become objects; the other arguments go on to clang as
// they were, with each `.smith` file replaced by its object. Experimental and off by default: without the flag a
// `.smith` file still goes through the token lowering. Included by smith.c; the units runner (SMITH_CORE) has no front
// end linked, so it has none of this.
#ifndef SMITH_CORE

// front::compile's entry (front/compile.smith, `front_compile_run`), declared here by hand: the generated header
// .smith/include/front/compile.h is made by the compile of a module, which the previous release's build (the lag rule)
// does not do for the sources under .smith/front/.
int32_t front_compile_run(const char *const *sources, const char *const *outputs, size_t count, const char *triple);

// front::parser's fix entry (front/parser.smith, `front_parser_fix_run`), declared by hand for the same reason.
int32_t front_parser_fix_run(const char *const *paths, size_t count);

constexpr size_t smith_front_args_max = 1024;
constexpr size_t smith_front_sources_max = 64;
constexpr size_t smith_front_name_max = 4096;

static char *smith_front_arguments[smith_front_args_max + 8];
static const char *smith_front_sources[smith_front_sources_max];
static const char *smith_front_objects[smith_front_sources_max];
static char smith_front_names[smith_front_sources_max][smith_front_name_max];

// The object for `source`: `output` when `-c -o output` names the only file, else `<stem>.o` in the current folder
// (when only compiling) or in smith's temporary folder (when linking).
static void smith_front_object_name(const char *source, const char *output, bool compile_only, size_t index) {
    char *name = smith_front_names[index];
    if (output != nullptr) {
        int written = snprintf(name, smith_front_name_max, "%s", output);
        SMITH_ASSERT(written > 0 && (size_t)written < smith_front_name_max);
        return;
    }
    const char *slash = strrchr(source, '/');
    const char *stem = slash != nullptr ? slash + 1 : source;
    const char *folder = compile_only ? "." : (getenv("TMPDIR") != nullptr ? getenv("TMPDIR") : "/tmp");
    int written = snprintf(name, smith_front_name_max, "%s/%.*s.%s", folder, (int)(strlen(stem) - strlen(".smith")),
                           stem, compile_only ? "o" : "front.o");
    SMITH_ASSERT(written > 0 && (size_t)written < smith_front_name_max);
}

static bool smith_front_is_source(const char *argument) {
    size_t length = strlen(argument);
    return length > strlen(".smith") && strcmp(argument + length - strlen(".smith"), ".smith") == 0;
}

// Whether `--front-end=new` is among the arguments; any other value of the flag is an error.
static bool smith_front_requested(int count, char **args) {
    bool requested = false;
    for (int i = 0; i < count; i++) { // bounded by count
        if (strncmp(args[i], "--front-end=", 12) != 0) continue;
        if (strcmp(args[i] + 12, "new") != 0) smith_die(smith_exit_usage, "--front-end takes only `new`", args[i]);
        requested = true;
    }
    return requested;
}

// Compiles the `.smith` files among `args` (argv[first ..]) with the front end and rewrites the arguments for clang in
// smith_front_arguments (argv[0 .. first) first): returns their number, or -1 when a file did not compile (its
// diagnostics are printed) and 0 when nothing is left for clang to do.
static int smith_front_rewrite(char **argv, int first, int count, char **args) {
    SMITH_ASSERT((size_t)count < smith_front_args_max && first >= 1 && first <= 8);
    for (int i = 0; i < first; i++) // bounded by 8
        smith_front_arguments[i] = argv[i];
    bool compile_only = false;
    const char *output = nullptr;
    const char *triple = nullptr;
    size_t sources = 0;
    for (int i = 0; i < count; i++) { // bounded by count
        if (strcmp(args[i], "-c") == 0) compile_only = true;
        if (strcmp(args[i], "-o") == 0 && i + 1 < count) output = args[i + 1];
        if (strncmp(args[i], "--target=", 9) == 0) triple = args[i] + 9;
        if (smith_front_is_source(args[i]) && i > 0 && strcmp(args[i - 1], "-o") != 0) {
            if (sources == smith_front_sources_max) smith_die(smith_exit_usage, "too many .smith files", nullptr);
            smith_front_sources[sources++] = args[i];
        }
    }
    bool single = compile_only && sources == 1;
    for (size_t i = 0; i < sources; i++) { // bounded by the arguments
        smith_front_object_name(smith_front_sources[i], single ? output : nullptr, compile_only, i);
        smith_front_objects[i] = smith_front_names[i];
    }
    if (sources > 0 && front_compile_run(smith_front_sources, smith_front_objects, sources, triple) != 0) return -1;
    int used = first;
    size_t seen = 0;
    for (int i = 0; i < count; i++) { // bounded by count
        if (strncmp(args[i], "--front-end=", 12) == 0) continue;
        if (smith_front_is_source(args[i]) && (i == 0 || strcmp(args[i - 1], "-o") != 0)) {
            if (!compile_only) smith_front_arguments[used++] = smith_front_names[seen];
            seen++;
            continue;
        }
        smith_front_arguments[used++] = args[i];
    }
    bool only_options = true;            // nothing but -c and -o <file> left: clang has no input to compile
    for (int i = first; i < used; i++) { // bounded by used
        if (strcmp(smith_front_arguments[i], "-o") == 0 && i + 1 < used)
            i++;
        else if (strcmp(smith_front_arguments[i], "-c") != 0)
            only_options = false;
    }
    return compile_only && only_options ? 0 : used;
}
#endif // SMITH_CORE

// Whether `smith lint --fix FILE...` is the front end's: `--fix` first and no `--`. With a `--` it is clang-tidy's
// `--fix` on a `.smith` file with the flags of its compile (smith-check), as before.
static bool smith_lint_fix_requested(int count, char **args) {
    if (count < 1 || strcmp(args[0], "--fix") != 0) return false;
    for (int i = 1; i < count; i++) { // bounded by count
        if (strcmp(args[i], "--") == 0) return false;
    }
    return true;
}

// `smith lint --fix FILE...` (#1442): each `.smith` file is parsed by smith's own front end, the fixes its parse errors
// carry are applied, and `path: N fixes applied` is printed; a file with nothing to fix reports 0. The units runner has
// no front end, so it refuses.
static int smith_lint_fix(int count, char **args) {
#ifdef SMITH_CORE
    (void)count;
    (void)args;
    smith_die(smith_exit_usage, "lint --fix needs the full smith (smith's own front end is not in this one)", nullptr);
#else
    if (count < 1) smith_die(smith_exit_usage, "usage: smith lint --fix FILE.smith...", nullptr);
    for (int i = 0; i < count; i++) {
        if (!smith_ends_with(args[i], ".smith"))
            smith_die(smith_exit_usage, "lint --fix reads .smith files only", args[i]);
    }
    return front_parser_fix_run((const char *const *)args, (size_t)count) == 0 ? 0 : 1;
#endif
}
