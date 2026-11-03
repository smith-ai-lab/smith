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

// front::compile's header entry (`front_header_run`), declared by hand for the same reason.
int32_t front_header_run(const char *source, const char *output, const char *triple);

// front::lsp_server's entry (`front_lsp_run`, `smith lsp --smith`), declared by hand for the same reason.
int32_t front_lsp_run(void);

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

// The include paths and macros of the command line, one on each line after `used` bytes of `flags`: a header is read
// with the `-I`, `-isystem`, `-iquote`, `-D`, `-U`, `-include` and `-std=` the compile has. What does not fit is left
// out.
static void smith_front_command_flags(char *flags, size_t size, size_t used, int count, char **args) {
    static const char *const separate[] = {"-I", "-isystem", "-iquote", "-D", "-U", "-include"};
    for (int i = 0; i < count; i++) { // bounded by count
        const char *argument = args[i];
        bool take = strncmp(argument, "-std=", 5) == 0;
        for (size_t k = 0; k < sizeof separate / sizeof separate[0] && !take; k++)
            take = strncmp(argument, separate[k], strlen(separate[k])) == 0;
        if (!take) continue;
        bool pair = false;
        for (size_t k = 0; k < sizeof separate / sizeof separate[0]; k++)
            pair = pair || strcmp(argument, separate[k]) == 0;
        if (pair && i + 1 >= count) break;
        int more = snprintf(flags + used, size - used, pair ? "%s\n%s\n" : "%s\n", argument, pair ? args[i + 1] : "");
        if (more < 0 || (size_t)more >= size - used) {
            flags[used] = '\0'; // no room: leave it out
            break;
        }
        used += (size_t)more;
        if (pair) i++;
    }
}

// The flags front::cimport reads a C header with (#1762), one on each line, in $SMITH_FRONT_CIMPORT_FLAGS for
// front::compile: the compiler's resource directory and, where there is one, the SDK. The caller's own value stays.
static void smith_front_cimport_flags(const char *self, int count, char **args) {
    if (getenv("SMITH_FRONT_CIMPORT_FLAGS") != nullptr) return;
    char folder[PATH_MAX];
    char programs[PATH_MAX];
    char resource[PATH_MAX];
    smith_resource_folder(folder);
    smith_programs_folder(folder, programs);
    smith_resource_ensure(folder, programs, self);
    smith_resource_directory(folder, resource);
#ifdef __APPLE__
    smith_sdk_environment(self);
#endif
    static char flags[1U << 16];
    int written = snprintf(flags, sizeof flags, "-resource-dir\n%s\n", resource);
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof flags);
    const char *sdk = getenv("SDKROOT");
    if (sdk != nullptr && *sdk != '\0' && strlen(sdk) < PATH_MAX)
        written += snprintf(flags + written, sizeof flags - (size_t)written, "-isysroot\n%s\n", sdk);
    smith_front_command_flags(flags, sizeof flags, (size_t)written, count, args);
    setenv("SMITH_FRONT_CIMPORT_FLAGS", flags, 1);
}

// `-g` (or `-g<level>`, not `-g0`) on the command line sets $SMITH_FRONT_DEBUG to 1 for front::compile, which then
// emits debug info (#1979): the line table, the source names and the variables `smith dbg`, `dap` and `symbolize`
// read. Without `-g` it is unset, so an object has none.
static void smith_front_debug_flag(int count, char **args) {
    bool debug = false;
    for (int i = 0; i < count; i++) { // bounded by count
        if (strncmp(args[i], "-g", 2) != 0 || strcmp(args[i], "-g0") == 0) continue;
        debug = strcmp(args[i], "-gdwarf-aranges") != 0;
    }
    if (debug) {
        setenv("SMITH_FRONT_DEBUG", "1", 1);
    } else {
        unsetenv("SMITH_FRONT_DEBUG");
    }
}

// Whether `--smith-header` is among the arguments: the new front end writes a header instead of an object (#1621).
static bool smith_front_header_requested(int count, char **args) {
    for (int i = 0; i < count; i++) { // bounded by count
        if (strcmp(args[i], "--smith-header") == 0) return true;
    }
    return false;
}

// `--front-end=new --smith-header src.smith -o out.h [--target=T]`: exactly one source and one `-o`. Returns 0 (nothing
// is left for clang) or -1 when the header was not written.
static int smith_front_header(int count, char **args, size_t sources, const char *output, const char *triple) {
    int outputs = 0;
    for (int i = 0; i < count; i++) // bounded by count
        outputs += strcmp(args[i], "-o") == 0 ? 1 : 0;
    if (sources != 1 || outputs != 1 || output == nullptr)
        smith_die(smith_exit_usage, "usage: smith cc --front-end=new --smith-header SOURCE.smith -o OUTPUT.h", nullptr);
    return front_header_run(smith_front_sources[0], output, triple) == 0 ? 0 : -1;
}

// Compiles the `.smith` files among `args` (argv[first ..]) with the front end and rewrites the arguments for clang in
// smith_front_arguments (argv[0 .. first) first): returns their number, or -1 when a file did not compile (its
// diagnostics are printed) and 0 when nothing is left for clang to do.
static int smith_front_rewrite(char **argv, int first, int count, char **args, const char *self) {
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
    if (smith_front_header_requested(count, args)) return smith_front_header(count, args, sources, output, triple);
    bool single = compile_only && sources == 1;
    for (size_t i = 0; i < sources; i++) { // bounded by the arguments
        smith_front_object_name(smith_front_sources[i], single ? output : nullptr, compile_only, i);
        smith_front_objects[i] = smith_front_names[i];
    }
    if (sources > 0) smith_front_cimport_flags(self, count, args);
    if (sources > 0) smith_front_debug_flag(count, args);
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
