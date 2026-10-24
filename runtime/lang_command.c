// The rule for the command of a compile that names .smith inputs, shared by `smith cc` (smith.c) and `smith lsp`
// (shim/clangd.cpp, which applies it to every command clangd compiles with), so that what the CLI accepts the editor
// accepts, and they cannot drift. Included by smith.c, which has smith_ends_with and smith_takes_value.

// Whether an input is C++ or Objective-C++, which have no `_Defer`.
static bool smith_lang_has_other_input(int argc, char **argv, int first) {
    static const char *const other[] = {".cpp", ".cc", ".cxx", ".C", ".mm", ".c++", ".cppm"};
    for (int i = first; i < argc; i++) { // bounded by argc
        if (argv[i][0] == '-') continue;
        for (size_t k = 0; k < sizeof other / sizeof other[0]; k++) { // bounded by the table
            if (smith_ends_with(argv[i], other[k])) return true;
        }
    }
    return false;
}

// Whether the command names a C standard, or has an input that is not C (a C++ file refuses a C standard): `.smith`
// is smith's own language and compiles as C2y, the standard `smith new` uses, unless the caller chose one.
static bool smith_lang_standard_given(int argc, char **argv, int first) {
    static const char *const other[] = {".cpp", ".cc", ".cxx", ".C", ".mm", ".c++", ".cppm"};
    for (int i = first; i < argc; i++) { // bounded by argc
        if (strncmp(argv[i], "-std=", 5) == 0 || strncmp(argv[i], "--std=", 6) == 0 || strcmp(argv[i], "-std") == 0 ||
            strcmp(argv[i], "--std") == 0) {
            return true;
        }
        if (argv[i][0] == '-') continue;
        for (size_t k = 0; k < sizeof other / sizeof other[0]; k++) { // bounded by the table
            if (smith_ends_with(argv[i], other[k])) return true;
        }
    }
    return false;
}

// Whether the argument at `i` is `-x c` (or `-xc`), or the same for a header, which is how clangd reads a command that
// it borrowed from a file of another extension (`-x c-header`).
static bool smith_lang_says_c(char **argv, int i, int argc) {
    if (strcmp(argv[i], "-xc") == 0 || strcmp(argv[i], "-xc-header") == 0) return true;
    if (strcmp(argv[i], "-x") != 0 || i + 1 >= argc) return false;
    return strcmp(argv[i + 1], "c") == 0 || strcmp(argv[i + 1], "c-header") == 0;
}

// What the scan of a command found.
typedef struct {
    bool found;    // a .smith input
    bool language; // it says `-x c` itself (any other language leaves the command alone)
    int dashes;    // the index of its `--`, after which every argument is an input (-1: none)
} SmithLangScan;

// Reads the command. False when it is left as it is: it names another language (-x) or is clang's -cc1, or has no
// .smith input.
static bool smith_lang_scan(int argc, char **argv, int first, SmithLangScan *scan) {
    *scan = (SmithLangScan){.dashes = -1};
    for (int i = first; i < argc; i++) { // bounded by argc
        if (scan->dashes >= 0) {
            scan->found = scan->found || smith_ends_with(argv[i], ".smith");
        } else if (smith_lang_says_c(argv, i, argc)) {
            scan->language = true;
            i += strcmp(argv[i], "-x") == 0 ? 1 : 0;
        } else if (strcmp(argv[i], "-cc1") == 0 || strncmp(argv[i], "-x", 2) == 0) {
            return false; // clang's -cc1, or another language
        } else if (strcmp(argv[i], "--") == 0) {
            scan->dashes = i;
        } else if (argv[i][0] == '-' && smith_takes_value(argv[i]) && i + 1 < argc) {
            i += 1;
        } else if (argv[i][0] != '-' && smith_ends_with(argv[i], ".smith")) {
            scan->found = true;
        }
    }
    return scan->found;
}

// The `--` of a command and the inputs after it, as they are; before them `-x c` when a .smith input is among them and
// the command does not say it. Returns the new count, or -1 when `out` is too small.
static int smith_lang_inputs_after(int argc, char **argv, int at, bool language, char **out, size_t used,
                                   size_t capacity) {
    bool after = false;                 // a .smith input follows the `--`
    for (int k = at + 1; k < argc; k++) // bounded by argc
        after = after || smith_ends_with(argv[k], ".smith");
    if (used + 2 + (size_t)(argc - at) > capacity) return -1;
    if (after && !language) {
        out[used++] = (char *)"-x";
        out[used++] = (char *)"c";
    }
    for (int k = at; k < argc; k++) // bounded by argc
        out[used++] = argv[k];
    out[used] = nullptr;
    return (int)used;
}

// A .smith file is C to the compiler, so each is given with `-x c` and the language is reset after it: `x.smith y.c`
// becomes `-x c x.smith -x none y.c`. The inputs after a `--` cannot be given a flag, so one `-x c` goes before it. A
// command that says `-x c` keeps it, and gets the standard like any other. The compiler's hook (shim/hook.cpp), which
// the .smith extension turns on, does the rest.
int smith_lang_command(int argc, char **argv, int first, char **out, size_t capacity) {
    SmithLangScan scan;
    if (!smith_lang_scan(argc, argv, first, &scan)) return 0;
    size_t used = 0;
    bool reset = false; // a .smith input was given, and the language is still C
    for (int i = 0; i < first; i++)
        out[used++] = argv[i];
    if (!smith_lang_standard_given(argc, argv, first)) out[used++] = (char *)"-std=gnu2y";
    // `defer` of a module is `_Defer`, which needs the defer TS on (issue #210); only a command with a .smith input
    // gets it, so a plain C file keeps C's keywords unless its own flags ask.
    for (int i = first; i < argc && !scan.language && !smith_lang_has_other_input(argc, argv, first); i++) { // bounded
        if (argv[i][0] != '-' && smith_ends_with(argv[i], ".smith")) {
            out[used++] = (char *)"-fdefer-ts";
            break;
        }
    }
    for (int i = first; i < argc; i++) { // bounded by argc
        if (used + 7 > capacity) return -1;
        if (i == scan.dashes) return smith_lang_inputs_after(argc, argv, i, scan.language, out, used, capacity);
        bool value = argv[i][0] == '-' && smith_takes_value(argv[i]) && i + 1 < argc;
        bool smith_input = argv[i][0] != '-' && smith_ends_with(argv[i], ".smith");
        if (value) {
            out[used++] = argv[i];
            out[used++] = argv[++i];
        } else if (smith_input && !scan.language) {
            out[used++] = (char *)"-x";
            out[used++] = (char *)"c";
            out[used++] = argv[i];
            reset = true;
        } else {
            if (reset && argv[i][0] != '-' && argv[i][0] != '@') {
                out[used++] = (char *)"-x";
                out[used++] = (char *)"none";
                reset = false;
            }
            out[used++] = argv[i];
        }
    }
    out[used] = nullptr;
    return (int)used;
}
