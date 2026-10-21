// The verbs that act on a project by what it declares (issue #40): fmt, lint, prove, test, run and check. Each means
// the same thing in every project and takes what it needs from the project itself, not from a list of its own:
//   fmt    the C, C++, Objective-C and .smith files git sees, formatted to .clang-format (clang-format honours
//          .clang-format-ignore itself)
//   lint   clang-tidy, with .clang-tidy, on the project's own sources among the build's compiles, in parallel
//   prove  the contracts of those sources, with the flags of their compiles
//   test   the tests smith_tests declares; the arguments go to the runner
//   run    the program the build's default names; the arguments go to the program
//   check  fmt, docs, lint, test and prove, each reported, the exit status 1 if any failed
// What a verb needs from build.c it reads from .smith/build.info, which every run of build.c writes (docs/build-c.md).
// Included by smith.c.
constexpr size_t smith_verb_args_max = 16384;
constexpr size_t smith_verbs_files_max = 16384;
constexpr size_t smith_verbs_names_max = 16U << 20;
constexpr size_t smith_verbs_batch = 128;
constexpr size_t smith_verbs_info_max = 8U << 20;
constexpr size_t smith_verbs_lines_max = 8192;
constexpr size_t smith_verbs_jobs_max = 64;
constexpr size_t smith_verbs_words_max = 8192;

// ---- Arguments

// Appends ";" and the argument to the text of -DSMITH_ARGS, with "%", ";" and newline as %25, %3B and %0A, so that no
// character of an argument is a separator or a newline (build.c's regeneration rule holds every -D option as a command
// word, and a command word holds none). smith_args in the runtime decodes it. Returns the new length.
static size_t smith_encode_argument(char *out, size_t capacity, size_t used, const char *arg) {
    SMITH_ASSERT(used < capacity);
    if (used + 2 > capacity) smith_die(smith_exit_usage, "the arguments are too long", arg);
    out[used++] = ';';
    for (const char *c = arg; *c != '\0'; c++) { // bounded by the argument
        const char *piece = *c == '%' ? "%25" : *c == ';' ? "%3B" : *c == '\n' ? "%0A" : nullptr;
        size_t length = piece != nullptr ? 3 : 1;
        if (used + length + 1 > capacity) smith_die(smith_exit_usage, "the arguments are too long", arg);
        if (piece != nullptr)
            memcpy(out + used, piece, length);
        else
            out[used] = *c;
        used += length;
    }
    out[used] = '\0';
    return used;
}

// A verb's arguments: the -D options are the build's, and the others (everything after `--` too) are the verb's own.
typedef struct {
    char *options[smith_build_args_max];
    size_t option_count;
    char *own[smith_build_args_max];
    size_t own_count;
} SmithVerbArgs;

static SmithVerbArgs smith_verb_args;

static SmithVerbArgs *smith_verb_split(int count, char **args) {
    SMITH_ASSERT(count >= 0);
    if ((size_t)count > smith_build_args_max) smith_die(smith_exit_usage, "too many arguments", nullptr);
    SmithVerbArgs *split = &smith_verb_args;
    *split = (SmithVerbArgs){};
    bool options_end = false;
    for (int i = 0; i < count; i++) { // bounded by count, which is checked above
        if (!options_end && strcmp(args[i], "--") == 0) {
            options_end = true;
        } else if (!options_end && strncmp(args[i], "-D", 2) == 0) {
            split->options[split->option_count++] = args[i];
        } else {
            split->own[split->own_count++] = args[i];
        }
    }
    return split;
}

// Builds `targets` (none: the build's default), the verb's -D options as the build's. With pass_args the verb's own
// arguments go to build.c as -DSMITH_ARGS, which smith_args reads (the test runner's options).
static int smith_verb_build(const char *self, const SmithVerbArgs *split, const char *const *targets, bool pass_args) {
    static char *build_args[smith_build_args_max + 8];
    static char joined[smith_verb_args_max] = "-DSMITH_ARGS=";
    constexpr size_t prefix = sizeof "-DSMITH_ARGS=" - 1;
    size_t used = 0;
    for (size_t i = 0; i < split->option_count; i++) // bounded by smith_build_args_max
        build_args[used++] = split->options[i];
    size_t joined_used = prefix;
    for (size_t i = 0; pass_args && i < split->own_count; i++) // bounded by smith_build_args_max
        joined_used = smith_encode_argument(joined, sizeof joined, joined_used, split->own[i]);
    joined[joined_used] = '\0';
    if (joined_used > prefix) build_args[used++] = joined;
    for (size_t i = 0; targets[i] != nullptr && i < 4; i++) // a verb names at most four targets
        build_args[used++] = (char *)targets[i];
    return smith_build_main((int)used, build_args, self, nullptr);
}

// ---- What the build wrote

typedef struct {
    const char *compiles[smith_verbs_lines_max]; // sources; compile_flags[i] is the rest of the line, tab separated
    const char *compile_flags[smith_verbs_lines_max];
    size_t compile_count;
    const char *programs[smith_verbs_lines_max];
    size_t program_count;
    const char *units[smith_verbs_lines_max]; // the sources of a program, tab separated
    size_t unit_count;
    const char *defaults[smith_verbs_lines_max];
    size_t default_count;
    const char *runners[smith_verbs_lines_max]; // the programs smith_tests made (a build.info without them has none)
    size_t runner_count;
    size_t test_count;
} SmithInfo;

static SmithInfo smith_info;
static char smith_info_text[smith_verbs_info_max + 1];

// Reads .smith/build.info, which build.c's run wrote: `compile<TAB>source<TAB>flag...`, `test<TAB>task`,
// `program<TAB>path`, `runner<TAB>path`, `default<TAB>target` (runtime/build_runtime.c). Each line is cut at its end in
// place.
static const SmithInfo *smith_info_read(const char *verb) {
    FILE *file = fopen(".smith/build.info", "rb");
    if (file == nullptr) smith_die(smith_exit_usage, verb, "the build left no .smith/build.info; is there a build.c?");
    size_t size = fread(smith_info_text, 1, smith_verbs_info_max, file);
    fclose(file);
    smith_info_text[size] = '\0';
    smith_info = (SmithInfo){};
    char *line = smith_info_text;
    for (size_t guard = 0; *line != '\0' && guard < 4 * smith_verbs_lines_max; guard++) { // bounded by the lines
        char *end = strchr(line, '\n');
        if (end == nullptr) end = line + strlen(line);
        bool last = *end == '\0';
        *end = '\0';
        char *tab = strchr(line, '\t');
        if (tab != nullptr) {
            *tab++ = '\0';
            if (strcmp(line, "compile") == 0 && smith_info.compile_count < smith_verbs_lines_max) {
                char *flags = strchr(tab, '\t');
                if (flags != nullptr) *flags++ = '\0';
                smith_info.compile_flags[smith_info.compile_count] = flags != nullptr ? flags : "";
                smith_info.compiles[smith_info.compile_count++] = tab;
            } else if (strcmp(line, "test") == 0) {
                smith_info.test_count++;
            } else if (strcmp(line, "unit") == 0 && smith_info.unit_count < smith_verbs_lines_max) {
                smith_info.units[smith_info.unit_count++] = tab;
            } else if (strcmp(line, "program") == 0 && smith_info.program_count < smith_verbs_lines_max) {
                smith_info.programs[smith_info.program_count++] = tab;
            } else if (strcmp(line, "runner") == 0 && smith_info.runner_count < smith_verbs_lines_max) {
                smith_info.runners[smith_info.runner_count++] = tab;
            } else if (strcmp(line, "default") == 0 && smith_info.default_count < smith_verbs_lines_max) {
                smith_info.defaults[smith_info.default_count++] = tab;
            }
        }
        if (last) break;
        line = end + 1;
    }
    return &smith_info;
}

// ---- Children

// Runs a child with its stdout and stderr on `log` when it is not nullptr; returns the pid.
static pid_t smith_verbs_start(char *const *argv, FILE *log) {
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (log != nullptr) {
        posix_spawn_file_actions_adddup2(&actions, fileno(log), STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, fileno(log), STDERR_FILENO);
    }
    fflush(stdout);
    fflush(stderr);
    pid_t pid = 0;
    int started = posix_spawn(&pid, argv[0], &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (started != 0) smith_die(smith_exit_missing, "cannot execute", argv[0]);
    return pid;
}

static int smith_verbs_status(int wait_status) {
    return WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : 128 + WTERMSIG(wait_status);
}

static int smith_verbs_wait(pid_t pid) {
    int wait_status = 0;
    pid_t waited = -1;
    for (int i = 0; i < 1000 && waited < 0; i++) // bounded; only a signal interrupts the wait
        waited = waitpid(pid, &wait_status, 0);
    return waited < 0 ? smith_exit_missing : smith_verbs_status(wait_status);
}

// Copies what a child wrote to its log to standard output, whole, so the output of children that ran side by side
// does not interleave.
static void smith_verbs_replay(FILE *log) {
    fseek(log, 0, SEEK_SET);
    char chunk[4096];
    size_t got = 0;
    for (size_t guard = 0; guard < (size_t)1 << 24 && (got = fread(chunk, 1, sizeof chunk, log)) > 0; guard++)
        fwrite(chunk, 1, got, stdout);
    fflush(stdout);
}

// Runs `count` jobs, `argv_of(i)` being the argv of job i, at most `jobs` at a time, each with its output kept apart
// and printed when it ends. Returns how many failed.
typedef struct {
    pid_t pid;
    FILE *log;
    size_t job;
} SmithVerbsSlot;

static size_t smith_verbs_parallel(size_t count, size_t jobs, char *const *(*argv_of)(size_t job, void *context),
                                   void *context) {
    SMITH_ASSERT(jobs >= 1 && jobs <= smith_verbs_jobs_max);
    SmithVerbsSlot slots[smith_verbs_jobs_max] = {};
    size_t next = 0;
    size_t active = 0;
    size_t failed = 0;
    for (size_t guard = 0; guard < 2 * count + 2 && (next < count || active > 0); guard++) { // each job starts, ends
        while (next < count && active < jobs) {
            size_t at = 0;
            while (slots[at].pid != 0) // a free slot: fewer than `jobs` are active
                at++;
            slots[at].log = tmpfile();
            if (slots[at].log == nullptr) smith_die(smith_exit_missing, "cannot make a temporary file", nullptr);
            slots[at].job = next;
            slots[at].pid = smith_verbs_start(argv_of(next, context), slots[at].log);
            next++;
            active++;
        }
        int wait_status = 0;
        pid_t done = waitpid(-1, &wait_status, 0);
        if (done < 0) continue;
        for (size_t at = 0; at < jobs; at++) { // bounded by the slots
            if (slots[at].pid != done) continue;
            if (smith_verbs_status(wait_status) != 0) failed++;
            smith_verbs_replay(slots[at].log);
            fclose(slots[at].log);
            slots[at] = (SmithVerbsSlot){};
            active--;
        }
    }
    return failed;
}

// ---- fmt

static bool smith_fmt_wanted(const char *name) {
    static const char *const extensions[] = {".c", ".h", ".m", ".mm", ".cpp", ".cc", ".cxx", ".hpp", ".hh", ".smith"};
    for (size_t i = 0; i < sizeof extensions / sizeof extensions[0]; i++) {
        if (smith_ends_with(name, extensions[i])) return true;
    }
    return false;
}

static char smith_fmt_names[smith_verbs_names_max];
static const char *smith_fmt_files[smith_verbs_files_max + 1];

static void smith_fmt_no_list(void) {
    fputs("smith: this folder is not a git repository, so smith fmt has no list of files; name them: smith fmt -i "
          "<files>\n",
          stderr);
}

// The files git sees: tracked, and untracked but not ignored; only those that are there (a file removed and not yet
// staged is still listed) and wanted by extension, in smith_fmt_files. NUL-separated names, so no name is mistaken.
// False, with a message, when git cannot list them.
static bool smith_fmt_list(size_t *count) {
#ifdef __APPLE__
    SmithIo io = smith_io_make(0, nullptr);
    const char *const argv[] = {"git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", nullptr};
    SmithProcessResult result = smith_processes_run(&io.processes, &io.arena, argv);
    if (result.error.code != 0 || result.exit_status != 0) {
        smith_fmt_no_list();
        smith_io_close(&io);
        return false;
    }
    if (result.output_size >= smith_verbs_names_max) smith_die(smith_exit_usage, "too many files", nullptr);
    memcpy(smith_fmt_names, result.output, result.output_size);
    size_t size = result.output_size;
    smith_io_close(&io);
#else
    FILE *git = popen("git ls-files -z --cached --others --exclude-standard 2>/dev/null", "r");
    size_t size = git != nullptr ? fread(smith_fmt_names, 1, smith_verbs_names_max - 1, git) : 0;
    if (git == nullptr || pclose(git) != 0) {
        smith_fmt_no_list();
        return false;
    }
#endif
    smith_fmt_names[size] = '\0';
    *count = 0;
    for (size_t at = 0; at < size; at += strlen(smith_fmt_names + at) + 1) { // bounded by the output
        const char *name = smith_fmt_names + at;
        struct stat info;
        if (!smith_fmt_wanted(name) || lstat(name, &info) != 0 || !S_ISREG(info.st_mode)) continue;
        if (*count >= smith_verbs_files_max) smith_die(smith_exit_usage, "too many files to format", nullptr);
        smith_fmt_files[(*count)++] = name;
    }
    smith_fmt_files[*count] = nullptr;
    return true;
}

// `smith fmt` with no arguments: clang-format on every file git sees, in batches, in place; with `check`, its dry run,
// which fails on a file that would change.
static int smith_fmt_project(const char *self, bool check) {
    size_t count = 0;
    if (!smith_fmt_list(&count)) return smith_exit_usage;
    const char *const *files = smith_fmt_files;
    if (count == 0) {
        fprintf(stderr, "smith fmt: git sees no C, C++, Objective-C or .smith files here\n");
        return 0;
    }
    int status = 0;
    for (size_t first = 0; first < count; first += smith_verbs_batch) { // bounded by the files
        char *argv[smith_verbs_batch + 6];
        size_t used = 0;
        argv[used++] = (char *)self;
        argv[used++] = (char *)"fmt";
        argv[used++] = (char *)(check ? "--dry-run" : "-i");
        if (check) argv[used++] = (char *)"--Werror";
        for (size_t i = first; i < count && i < first + smith_verbs_batch; i++)
            argv[used++] = (char *)files[i];
        argv[used] = nullptr;
        int one = smith_verbs_wait(smith_verbs_start(argv, nullptr));
        if (one != 0) status = one;
    }
    if (status != 0 && check) fprintf(stderr, "smith fmt: some files are not formatted; `smith fmt` formats them\n");
    return status;
}

// ---- lint

static char *smith_lint_argv[8];

static char *const *smith_lint_job(size_t job, void *context) {
    const char *const *files = (const char *const *)context;
    smith_lint_argv[5] = (char *)files[job];
    return smith_lint_argv;
}

static size_t smith_verbs_jobs(void) {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1) return 1;
    return (size_t)cores > smith_verbs_jobs_max ? smith_verbs_jobs_max : (size_t)cores;
}

// `smith lint` with no arguments: builds what the project's compiles read (the generated headers), then runs clang-tidy
// on each of its own C-family sources, side by side, with the flags of the compile (.smith/compile_commands.json). A
// `.smith` source is linted as written (shim/tidy.cpp) by a smith whose clang has the token hook; the others (the
// stage0) leave them out and say so once, as `smith cc` refuses them.
static int smith_lint_project(int count, char **args, const char *self) {
    SmithVerbArgs *split = smith_verb_split(count, args);
    const char *const targets[] = {"smith_needs", nullptr};
    int status = smith_verb_build(self, split, targets, false);
    if (status != 0) return status;
    const SmithInfo *info = smith_info_read("smith lint");
    static const char *files[smith_verbs_lines_max + 1];
    size_t files_count = 0;
    size_t skipped = 0;
    for (size_t i = 0; i < info->compile_count; i++) {
#ifndef SMITH_HOOK // the condition under which smith_route_clang refuses a .smith input
        if (smith_ends_with(info->compiles[i], ".smith")) {
            skipped += 1;
            continue;
        }
#endif
        files[files_count++] = info->compiles[i];
    }
    if (skipped > 0) {
        fprintf(stderr, "smith lint: %zu .smith source(s) left out: this smith's clang has no token hook\n", skipped);
    }
    if (files_count == 0) {
        fprintf(stderr, "smith lint: the build compiles no C or C++ source of the project's own\n");
        return 0;
    }
    smith_lint_argv[0] = (char *)self;
    smith_lint_argv[1] = (char *)"lint";
    smith_lint_argv[2] = (char *)"--quiet";
    smith_lint_argv[3] = (char *)"-p";
    smith_lint_argv[4] = (char *)".smith";
    smith_lint_argv[6] = nullptr;
    size_t failed = smith_verbs_parallel(files_count, smith_verbs_jobs(), smith_lint_job, (void *)files);
    if (failed > 0) fprintf(stderr, "smith lint: clang-tidy failed on %zu of %zu files\n", failed, files_count);
    return failed > 0 ? 1 : 0;
}

// ---- prove

static bool smith_has_prover(void) {
#if defined(SMITH_PROVE) && defined(SMITH_IN_PROCESS)
    return true;
#else
    return false;
#endif
}

// The words of a compile that the prover must not see: the depfile and output, and what only instruments or caches
// (sanitizers, modules, frame pointers, pattern initialization) and would change the IR that is proved.
static bool smith_prove_drops(const char *word, bool *skip_next) {
    if (*skip_next) {
        *skip_next = false;
        return true;
    }
    if (strcmp(word, "-MF") == 0 || strcmp(word, "-c") == 0 || strcmp(word, "-o") == 0) {
        *skip_next = true;
        return true;
    }
    static const char *const prefixes[] = {
        "-MMD", "-fsanitize", "-fno-sanitize", "-fmodule", "-fno-omit-frame-pointer", "-ftrivial-auto-var-init"};
    for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++) {
        if (strncmp(word, prefixes[i], strlen(prefixes[i])) == 0) return true;
    }
    return false;
}

// The sources `smith prove` reads: C files, not build.c (which is a program that writes the manifest).
static bool smith_proves(const char *source) {
    return smith_ends_with(source, ".c") && strcmp(source, "build.c") != 0;
}

static char *smith_prove_argv[smith_verbs_lines_max + smith_verbs_words_max + 8];

// Keeps, in place, the words of a tab-separated compile that the prover may see (smith_prove_drops).
static void smith_prove_filter(char *flags) {
    char *start = flags;
    char *write = flags;
    bool skip = false;
    for (size_t guard = 0; guard < smith_verbs_words_max; guard++) { // bounded by the words
        char *tab = strchr(flags, '\t');
        size_t length = tab != nullptr ? (size_t)(tab - flags) : strlen(flags);
        char saved = flags[length];
        flags[length] = '\0';
        if (!smith_prove_drops(flags, &skip)) {
            if (write != flags) memmove(write, flags, length + 1);
            write += length;
            *write++ = '\t';
        }
        flags[length] = saved;
        if (tab == nullptr) break;
        flags = tab + 1;
    }
    if (write > start) write--; // the tab after the last word is the end
    *write = '\0';
}

// Proves one group of sources that compile with the same flags: `smith prove <sources> -- <flags>`.
static int smith_prove_group(const char *self, const SmithInfo *info, const size_t *members, size_t member_count) {
    size_t used = 0;
    smith_prove_argv[used++] = (char *)self;
    smith_prove_argv[used++] = (char *)"prove";
    for (size_t i = 0; i < member_count; i++)
        smith_prove_argv[used++] = (char *)info->compiles[members[i]];
    smith_prove_argv[used++] = (char *)"--";
    char *flags = (char *)info->compile_flags[members[0]];
    for (size_t guard = 0; *flags != '\0' && guard < smith_verbs_words_max; guard++) { // bounded by the words
        char *tab = strchr(flags, '\t');
        if (tab != nullptr) *tab = '\0';
        if (used + 2 >= sizeof smith_prove_argv / sizeof smith_prove_argv[0]) break;
        if (*flags != '\0') smith_prove_argv[used++] = flags;
        if (tab == nullptr) break;
        flags = tab + 1;
    }
    smith_prove_argv[used] = nullptr;
    return smith_verbs_wait(smith_verbs_start(smith_prove_argv, nullptr));
}

// The index of the compile of this source, or smith_verbs_lines_max.
static size_t smith_prove_compile_of(const SmithInfo *info, const char *source) {
    for (size_t i = 0; i < info->compile_count; i++) {
        if (strcmp(info->compiles[i], source) == 0) return i;
    }
    return smith_verbs_lines_max;
}

// Proves one program's sources: those with the same flags together (the prover reads one set of flags), as the program
// links them.
static int smith_prove_unit(const char *self, const SmithInfo *info, char *sources) {
    static size_t found[smith_verbs_lines_max];
    size_t found_count = 0;
    for (char *source = sources; source != nullptr && *source != '\0' && found_count < smith_verbs_lines_max;) {
        char *tab = strchr(source, '\t');
        if (tab != nullptr) *tab = '\0';
        size_t at = smith_prove_compile_of(info, source);
        if (at < smith_verbs_lines_max && smith_proves(source)) found[found_count++] = at;
        source = tab != nullptr ? tab + 1 : nullptr;
    }
    static size_t members[smith_verbs_lines_max];
    static bool done[smith_verbs_lines_max];
    memset(done, 0, sizeof done);
    int status = 0;
    for (size_t first = 0; first < found_count; first++) { // each source joins the group of its first flags
        if (done[first]) continue;
        size_t member_count = 0;
        for (size_t i = first; i < found_count; i++) {
            if (done[i] || strcmp(info->compile_flags[found[i]], info->compile_flags[found[first]]) != 0) continue;
            done[i] = true;
            members[member_count++] = found[i];
        }
        int one = smith_prove_group(self, info, members, member_count);
        if (one != 0) status = one;
    }
    return status;
}

// `smith prove` with no arguments: the contracts of the sources each program of the build links (not the tests'
// runner), each compiled with the flags the build gives it, less what only instruments or caches (sanitizers and
// modules). Sources with the same flags are proved together; programs with the same sources once.
static int smith_prove_project(int count, char **args, const char *self) {
    if (!smith_has_prover()) {
        smith_die(smith_exit_usage, "this smith is the stage0, which has no prover; the full smith has it",
                  "smith build llvm, then smith build");
    }
    SmithVerbArgs *split = smith_verb_split(count, args);
    const char *const targets[] = {"smith_needs", nullptr};
    int status = smith_verb_build(self, split, targets, false);
    if (status != 0) return status;
    const SmithInfo *info = smith_info_read("smith prove");
    for (size_t i = 0; i < info->compile_count; i++) {
        if (smith_proves(info->compiles[i])) smith_prove_filter((char *)info->compile_flags[i]);
    }
    static bool repeated[smith_verbs_lines_max]; // compared before any unit is cut into its sources
    for (size_t i = 0; i < info->unit_count; i++) {
        for (size_t j = 0; j < i; j++)
            repeated[i] = repeated[i] || strcmp(info->units[i], info->units[j]) == 0;
    }
    status = 0;
    for (size_t i = 0; i < info->unit_count; i++) {
        if (repeated[i]) continue;
        int one = smith_prove_unit(self, info, (char *)info->units[i]);
        if (one != 0) status = one;
    }
    return status;
}

// ---- test and run

// `smith test`: builds and runs the tests smith_tests declares; the arguments go to the runner.
static int smith_test_project(int count, char **args, const char *self) {
    SmithVerbArgs *split = smith_verb_split(count, args);
    const char *const targets[] = {"smith_tests", nullptr};
    int status = smith_verb_build(self, split, targets, true);
    if (status != 0) return status;
    if (smith_info_read("smith test")->test_count == 0) {
        fprintf(stderr, "smith test: build.c declares no tests; smith_tests (<smith/build.h>) declares them\n");
    }
    return 0;
}

// `smith run`: builds the default, then runs the one program it names with the verb's arguments.
static int smith_run_project(int count, char **args, const char *self) {
    SmithVerbArgs *split = smith_verb_split(count, args);
    const char *const targets[] = {nullptr};
    int status = smith_verb_build(self, split, targets, false);
    if (status != 0) return status;
    const SmithInfo *info = smith_info_read("smith run");
    const char *named[smith_verbs_lines_max];
    size_t named_count = 0;
    for (size_t i = 0; i < info->default_count; i++) {
        for (size_t j = 0; j < info->program_count; j++) {
            if (strcmp(info->defaults[i], info->programs[j]) == 0) named[named_count++] = info->defaults[i];
        }
    }
    if (named_count != 1) {
        fprintf(stderr, "smith run: the default of build.c names %s program%s",
                named_count == 0 ? "no" : "more than one", named_count == 1 ? "" : "s");
        const char *separator = ": ";
        for (size_t i = 0; i < info->default_count; i++) {
            fprintf(stderr, "%s%s", separator, info->defaults[i]);
            separator = ", ";
        }
        fputs("\nsmith run runs the program that smith_default names, so name exactly one\n", stderr);
        return smith_exit_usage;
    }
    static char *run_argv[smith_build_args_max + 2];
    run_argv[0] = (char *)named[0];
    for (size_t i = 0; i < split->own_count; i++)
        run_argv[i + 1] = split->own[i];
    run_argv[split->own_count + 1] = nullptr;
    smith_run(named[0], run_argv);
    return smith_exit_missing;
}

// ---- check

static void smith_check_part(const char *name, int status, const char **failed, size_t *failed_count) {
    if (status == 0) {
        fprintf(stderr, "smith check: %s ok\n", name);
        return;
    }
    fprintf(stderr, "smith check: %s FAILED (status %d)\n", name, status);
    failed[(*failed_count)++] = name;
}

static int smith_check_child(const char *self, const char *verb, const SmithVerbArgs *split) {
    static char *argv[smith_build_args_max + 3];
    size_t used = 0;
    argv[used++] = (char *)self;
    argv[used++] = (char *)verb;
    for (size_t i = 0; i < split->option_count; i++)
        argv[used++] = split->options[i];
    argv[used] = nullptr;
    return smith_verbs_wait(smith_verbs_start(argv, nullptr));
}

// `smith check`: formatting (clang-format's dry run), the docs, lint, the tests and the proofs, in that order, cheapest
// first. A part that fails does not stop the rest: every part is reported, and the exit status is 1 if any failed.
static int smith_check_project(int count, char **args, const char *self) {
    SmithVerbArgs *split = smith_verb_split(count, args);
    if (split->own_count > 0) smith_die(smith_exit_usage, "takes no arguments but -D options", "check");
    const char *failed[8];
    size_t failed_count = 0;
    smith_check_part("fmt", smith_fmt_project(self, true), failed, &failed_count);
    smith_check_part("docs", smith_docs_check(0, nullptr), failed, &failed_count);
    if (access("build.c", R_OK) != 0) {
        fprintf(stderr, "smith check: no build.c, so lint, test and prove are skipped\n");
    } else {
        smith_check_part("lint", smith_check_child(self, "lint", split), failed, &failed_count);
        smith_check_part("test", smith_check_child(self, "test", split), failed, &failed_count);
        if (smith_has_prover()) {
            smith_check_part("prove", smith_check_child(self, "prove", split), failed, &failed_count);
        } else {
            fprintf(stderr, "smith check: this smith has no prover, so prove is skipped\n");
        }
    }
    if (failed_count == 0) {
        fprintf(stderr, "smith check: ok\n");
        return 0;
    }
    fprintf(stderr, "smith check: failed:");
    for (size_t i = 0; i < failed_count; i++)
        fprintf(stderr, " %s", failed[i]);
    fputc('\n', stderr);
    return 1;
}

// `smith fmt` with no arguments, in place.
static int smith_fmt_verb(int count, char **args, const char *self) {
    (void)smith_verb_split(count, args);
    return smith_fmt_project(self, false);
}
