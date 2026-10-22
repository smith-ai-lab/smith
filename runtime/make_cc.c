// Reading a recipe's command as a compile or a link: which words are the compiler's own and which are flags,
// libraries, sources and objects. Included by smith.c after make_emit.c.

typedef struct {
    const char *compiler;
    size_t *others; // flags, libraries and other words that are not the compile's own, in order
    size_t other_count;
    size_t *objects;
    size_t object_count;
    int64_t source;
    int64_t output;
    bool has_c;
    bool dropped_dependency_flags;
    size_t source_count;
} SmithMakeCc;

static const char *smith_make_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash != nullptr ? slash + 1 : path;
}

// "smith-cc" or "smith-c++" for a C or C++ compiler by whatever name, else nullptr.
static const char *smith_make_compiler_name(const char *text) {
    static const char *const c_names[] = {"cc", "gcc", "clang", "smith", "smith-cc"};
    static const char *const cxx_names[] = {"c++", "g++", "clang++", "smith++", "smith-c++"};
    const char *name = smith_make_basename(text);
    for (size_t i = 0; i < sizeof c_names / sizeof c_names[0]; i++) {
        if (strcmp(name, c_names[i]) == 0) return "smith-cc";
    }
    for (size_t i = 0; i < sizeof cxx_names / sizeof cxx_names[0]; i++) {
        if (strcmp(name, cxx_names[i]) == 0) return "smith-c++";
    }
    return nullptr;
}

static bool smith_make_is_source(const char *text) {
    static const char *const suffixes[] = {".c", ".cc", ".cpp", ".cxx", ".C", ".m", ".mm", ".S", ".s", ".smith"};
    for (size_t i = 0; i < sizeof suffixes / sizeof suffixes[0]; i++) {
        if (smith_make_has_suffix(text, suffixes[i])) return true;
    }
    return false;
}

// Flags whose value is the next word.
static bool smith_make_flag_takes_value(const char *flag) {
    static const char *const flags[] = {"-I",
                                        "-L",
                                        "-D",
                                        "-U",
                                        "-F",
                                        "-include",
                                        "-imacros",
                                        "-isystem",
                                        "-iquote",
                                        "-idirafter",
                                        "-isysroot",
                                        "-iframework",
                                        "-framework",
                                        "-x",
                                        "-arch",
                                        "-target",
                                        "-Xclang",
                                        "-Xlinker",
                                        "-Xpreprocessor",
                                        "-Xassembler",
                                        "-resource-dir",
                                        "-mllvm",
                                        "-rpath",
                                        "-install_name",
                                        "-T",
                                        "-u",
                                        "-z",
                                        "-working-directory",
                                        "-include-pch",
                                        "-weak_framework",
                                        "-exported_symbols_list",
                                        "-filelist",
                                        "-MJ"};
    for (size_t i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        if (strcmp(flag, flags[i]) == 0) return true;
    }
    return false;
}

static bool smith_make_is_dependency_flag(const char *flag, bool *takes_value) {
    *takes_value = strcmp(flag, "-MF") == 0 || strcmp(flag, "-MT") == 0 || strcmp(flag, "-MQ") == 0;
    return *takes_value || strcmp(flag, "-MMD") == 0 || strcmp(flag, "-MD") == 0 || strcmp(flag, "-MP") == 0 ||
           strcmp(flag, "-MM") == 0 || strcmp(flag, "-M") == 0;
}

// One word after the compiler. Returns false for a word this reading does not know (a file that is no source).
static bool smith_make_cc_word(const SmithMakeArgv *argv, SmithMakeCc *cc, size_t *at) {
    size_t i = *at;
    const char *text = argv->args[i].text;
    bool takes_value = false;
    if (text[0] == '-' && text[1] != '\0') {
        if (strcmp(text, "-c") == 0) {
            cc->has_c = true;
        } else if (strcmp(text, "-o") == 0 && i + 1 < argv->count) {
            cc->output = (int64_t)++i;
        } else if (smith_make_is_dependency_flag(text, &takes_value)) {
            cc->dropped_dependency_flags = true;
            if (takes_value) i++;
        } else {
            cc->others[cc->other_count++] = i;
            if (smith_make_flag_takes_value(text) && i + 1 < argv->count) cc->others[cc->other_count++] = ++i;
        }
    } else if (smith_make_is_source(text) && !smith_make_has_hole(text, strlen(text))) {
        cc->source = (int64_t)i;
        cc->source_count++;
    } else if (smith_make_has_suffix(text, ".o")) {
        cc->objects[cc->object_count++] = i;
    } else if (smith_make_has_hole(text, strlen(text)) || smith_make_has_suffix(text, ".a") ||
               smith_make_has_suffix(text, ".dylib") || smith_make_has_suffix(text, ".so")) {
        cc->others[cc->other_count++] = i; // a library, or a list that build.c reads when it runs
    } else {
        return false;
    }
    *at = i;
    return true;
}

// Reads a command as a compiler's: false when it is not one, or has words that this reading does not know.
static bool smith_make_parse_cc(const SmithMakeArgv *argv, SmithMakeCc *cc) {
    *cc = (SmithMakeCc){.source = -1, .output = -1};
    if (argv->count == 0) return false;
    cc->compiler = smith_make_compiler_name(argv->args[0].text);
    if (cc->compiler == nullptr) return false;
    cc->others = smith_make_alloc((argv->count + 1) * sizeof(size_t));
    cc->objects = smith_make_alloc((argv->count + 1) * sizeof(size_t));
    for (size_t i = 1; i < argv->count; i++) { // bounded by the words
        if (!smith_make_cc_word(argv, cc, &i)) return false;
    }
    return true;
}

static bool smith_make_is_compile(const SmithMakeCc *cc) {
    return cc->has_c && cc->source_count == 1 && cc->output >= 0 && cc->object_count == 0;
}

// A link: objects side by side. The words before them are flags, the words after are libraries.
static bool smith_make_is_link(const SmithMakeCc *cc) {
    if (cc->has_c || cc->source_count != 0 || cc->output < 0 || cc->object_count == 0) return false;
    for (size_t i = 1; i < cc->object_count; i++) {
        if (cc->objects[i] != cc->objects[i - 1] + 1) return false;
    }
    for (size_t i = 0; i < cc->other_count; i++) {
        if (cc->others[i] > cc->objects[0] && cc->others[i] < cc->objects[cc->object_count - 1]) return false;
    }
    return true;
}
