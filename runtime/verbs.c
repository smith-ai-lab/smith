// The verbs that act on a project by what it declares (issue #40): fmt, lint, prove, test, run and check. Each means
// the same thing in every project and takes what it needs from the project itself, not from a list of its own:
//   fmt    the C, C++, Objective-C and .smith files git sees, formatted to .clang-format (clang-format honours
//          .clang-format-ignore itself)
//   lint   clang-tidy, with .clang-tidy, on the project's own sources among the build's compiles, in parallel
//   prove  the contracts of those sources, with the flags of their compiles
//   test   the tests smith_tests declares; the arguments go to the runner
//   run    the program the build's default names; the arguments go to the program
//   check  fmt, docs, lint, test and prove, each reported, the exit status 1 if any failed
// What a verb needs from build.c it reads from .smith/build.info, which every run of build.c writes
// (knowledge/verbs.md). Included by smith.c.
#include <fnmatch.h>
#include <ftw.h>
constexpr size_t smith_verb_args_max = 16384;
constexpr size_t smith_verbs_files_max = 16384;
constexpr size_t smith_verbs_names_max = 16U << 20;
constexpr size_t smith_verbs_batch = 128;
constexpr size_t smith_verbs_info_max = 64U << 20; // .smith/build.info; bss, so only the pages a project fills are used
constexpr size_t smith_verbs_lines_max = 131072;   // lines of one kind (twice smith_objects_max)
constexpr size_t smith_verbs_jobs_max = 64;
constexpr size_t smith_verbs_words_max = 131072;

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
    const char *libraries[smith_verbs_lines_max]; // the library sources of the program of the same index in units
    size_t library_count;
    const char *defaults[smith_verbs_lines_max];
    size_t default_count;
    const char *runners[smith_verbs_lines_max]; // the programs smith_tests made (a build.info without them has none)
    size_t runner_count;
    size_t test_count;
} SmithInfo;

static SmithInfo smith_info;
static char smith_info_text[smith_verbs_info_max + 1];

[[noreturn]] static void smith_info_too_many(const char *things, size_t limit) {
    fprintf(stderr,
            "smith: .smith/build.info has more than %zu %s (the limit); split the build or raise the limit in "
            "runtime/verbs.c\n",
            limit, things);
    exit(smith_exit_usage);
}

static void smith_info_add(const char **list, size_t *count, const char *what, const char *value) {
    if (*count >= smith_verbs_lines_max) smith_info_too_many(what, smith_verbs_lines_max);
    list[(*count)++] = value;
}

// Reads .smith/build.info, which build.c's run wrote: `compile<TAB>source<TAB>flag...`, `test<TAB>task`,
// `program<TAB>path`, `runner<TAB>path`, `default<TAB>target` (runtime/build_runtime.c). Each line is cut at its end in
// place.
static const SmithInfo *smith_info_read(const char *verb) {
    FILE *file = fopen(".smith/build.info", "rb");
    if (file == nullptr)
        smith_die(smith_exit_usage, verb, "the build left no .smith/build.info; is there a build.c or build.smith?");
    size_t size = fread(smith_info_text, 1, smith_verbs_info_max + 1, file);
    fclose(file);
    if (size > smith_verbs_info_max) smith_info_too_many("bytes", smith_verbs_info_max);
    smith_info_text[size] = '\0';
    smith_info = (SmithInfo){};
    char *line = smith_info_text;
    for (size_t guard = 0; *line != '\0' && guard < smith_verbs_info_max; guard++) { // bounded by the bytes
        char *end = strchr(line, '\n');
        if (end == nullptr) end = line + strlen(line);
        bool last = *end == '\0';
        *end = '\0';
        char *tab = strchr(line, '\t');
        if (tab != nullptr || strcmp(line, "unit") == 0 || strcmp(line, "library") == 0) { // these two may be empty
            if (tab != nullptr) {
                *tab++ = '\0';
            } else {
                tab = end;
            }
            if (strcmp(line, "compile") == 0) {
                char *flags = strchr(tab, '\t');
                if (flags != nullptr) *flags++ = '\0';
                size_t at = smith_info.compile_count;
                smith_info_add(smith_info.compiles, &smith_info.compile_count, "compile lines", tab);
                smith_info.compile_flags[at] = flags != nullptr ? flags : "";
            } else if (strcmp(line, "test") == 0) {
                smith_info.test_count++;
            } else if (strcmp(line, "unit") == 0) {
                smith_info_add(smith_info.units, &smith_info.unit_count, "unit lines", tab);
            } else if (strcmp(line, "library") == 0) {
                smith_info_add(smith_info.libraries, &smith_info.library_count, "library lines", tab);
            } else if (strcmp(line, "program") == 0) {
                smith_info_add(smith_info.programs, &smith_info.program_count, "program lines", tab);
            } else if (strcmp(line, "runner") == 0) {
                smith_info_add(smith_info.runners, &smith_info.runner_count, "runner lines", tab);
            } else if (strcmp(line, "default") == 0) {
                smith_info_add(smith_info.defaults, &smith_info.default_count, "default lines", tab);
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

// With `ended`, each job's wait status and log go to it before the log is printed.
static size_t smith_verbs_parallel(size_t count, size_t jobs, char *const *(*argv_of)(size_t job, void *context),
                                   void (*ended)(size_t job, int wait_status, FILE *log, void *context),
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
            if (ended != nullptr) ended(slots[at].job, wait_status, slots[at].log, context);
            smith_verbs_replay(slots[at].log);
            fclose(slots[at].log);
            slots[at] = (SmithVerbsSlot){};
            active--;
        }
    }
    return failed;
}

// ---- Quiet runs (issue #78)

// What a part of the check cost: its wall time and the CPU time of this process and of the children it waited for.
typedef struct {
    uint64_t wall_ms;
    uint64_t cpu_ms;
} SmithCheckClock;

static SmithCheckClock smith_check_clock(void) {
    struct rusage own = {};
    struct rusage children = {};
    getrusage(RUSAGE_SELF, &own);
    getrusage(RUSAGE_CHILDREN, &children);
    return (SmithCheckClock){.wall_ms = smith_account_now_ms(),
                             .cpu_ms = smith_account_ms(own.ru_utime) + smith_account_ms(own.ru_stime) +
                                       smith_account_ms(children.ru_utime) + smith_account_ms(children.ru_stime)};
}

// Whether the run prints everything as it happens: `-v` (and `--verbose`, `--list`, `--dry-run` for the tests), or
// SMITH_OUTPUT=full.
static bool smith_quiet_loud(int count, char **args, bool runner_verbose) {
    if (smith_quiet_full()) return true;
    for (int i = 0; i < count; i++) {
        if (strcmp(args[i], "-v") == 0 || (runner_verbose && strcmp(args[i], "--verbose") == 0)) return true;
        // A listing is the answer, not progress: --list and --dry-run print it (issue #456).
        if (runner_verbose && (strcmp(args[i], "--list") == 0 || strcmp(args[i], "--dry-run") == 0)) return true;
    }
    return false;
}

// Runs `smith <verb> <args>` as a child that prints everything (SMITH_OUTPUT=full) into the log of the verb; returns
// its exit status. The log is `.smith/log/<verb>.log`, written whole by every run.
static int smith_quiet_child(const char *self, const char *verb, int count, char *const *args, const char *log_path) {
    static char *argv[smith_build_args_max + 4];
    if ((size_t)count > smith_build_args_max) smith_die(smith_exit_usage, "too many arguments", nullptr);
    size_t used = 0;
    argv[used++] = (char *)self;
    argv[used++] = (char *)verb;
    for (int i = 0; i < count; i++)
        argv[used++] = args[i];
    argv[used] = nullptr;
    FILE *log = fopen(log_path, "w");
    if (log == nullptr) smith_die(smith_exit_missing, "cannot write the log", log_path);
    setenv("SMITH_OUTPUT", "full", 1);
    int status = smith_verbs_wait(smith_verbs_start(argv, log));
    fclose(log);
    return status;
}

// What a stage that failed shows: the bounded excerpt of its log and where the rest is.
static void smith_quiet_failure(const char *label, const char *log_path, const char *text, size_t size) {
    static char excerpt[smith_quiet_excerpt_max];
    size_t hidden = 0;
    size_t length = smith_quiet_excerpt(excerpt, text, size, &hidden);
    fwrite(excerpt, 1, length, stderr);
    fprintf(stderr, "%s: %zu more lines; the full output is in %s\n", label, hidden, log_path);
}

// The second line of `smith prove` (issue #51): how much of the program has contracts, indented under the first.
static void smith_quiet_coverage_line(const char *label, const char *coverage) {
    size_t of = 0, total = 0;
    const char *at = coverage;
    if (coverage[0] == '\0' || !smith_quiet_field(&at, &of, " of ") || !smith_quiet_field(&at, &total, " functions"))
        return;
    int indent = (int)strlen(label) + 2;
    if (of < total) {
        fprintf(stderr, "%*s%zu of %zu functions have contracts; smith prove -v lists the %zu without\n", indent, "",
                of, total, total - of);
    } else {
        fprintf(stderr, "%*s%zu of %zu functions have contracts\n", indent, "", of, total);
    }
}

// The smith verb `verb` for a person or an agent: unless it is loud, runs as a child whose output goes to its log and
// ends with one line (a failure also shows the excerpt and the log's path). Returns -1 when the caller runs it as it
// is, the child's status otherwise.
static int smith_quiet_verb(const char *verb, int count, char **args, const char *self, bool runner_verbose) {
    if (smith_quiet_loud(count, args, runner_verbose)) return -1;
    char log[PATH_MAX];
    smith_quiet_log_path(log, sizeof log, verb);
    SmithCheckClock since = smith_check_clock();
    int status = smith_quiet_child(self, verb, count, args, log);
    SmithCheckClock now = smith_check_clock();
    double wall = (double)(now.wall_ms - since.wall_ms) / 1000.0;
    size_t size = 0;
    const char *text = smith_quiet_read(log, &size);
    char label[32];
    snprintf(label, sizeof label, "smith %s", verb);
    if (status == 0) {
        char detail[128], coverage[64];
        smith_quiet_detail(verb, text, size, detail, sizeof detail, coverage, sizeof coverage);
        fprintf(stderr, "%s: ok (%s%s%.1f s)\n", label, detail, detail[0] != '\0' ? ", " : "", wall);
        smith_quiet_coverage_line(label, coverage);
        return 0;
    }
    fprintf(stderr, "%s: FAILED (status %d, %.1f s)\n", label, status, wall);
    smith_quiet_failure(label, log, text, size);
    return status;
}

// ---- fmt

static bool smith_fmt_wanted(const char *name) {
    static const char *const extensions[] = {".c",   ".h",   ".m",  ".mm",    ".cpp",  ".cc",
                                             ".cxx", ".hpp", ".hh", ".smith", ".metal"};
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

static char *smith_lint_argv[16];

// The files to lint, the flags of the compile of each (nullptr: none, so no cache key) and, of the ones the cache did
// not answer, their numbers: job n lints files[order[n]].
typedef struct {
    const char *const *files;
    const char *const *flags;
    size_t *order;
} SmithLintRun;

static char *const *smith_lint_job(size_t job, void *context) {
    const SmithLintRun *run = (const SmithLintRun *)context;
    size_t at = 5 + smith_lc_extra(run->order[job], smith_lint_argv + 5);
    smith_lint_argv[at++] = (char *)run->files[run->order[job]];
    smith_lint_argv[at] = nullptr;
    return smith_lint_argv;
}

static void smith_lint_done(size_t job, int wait_status, FILE *log, void *context) {
    const SmithLintRun *run = (const SmithLintRun *)context;
    smith_lc_store(run->order[job], wait_status, log);
}

static size_t smith_verbs_jobs(void) {
    long cores = smith_slots_cores();
    return (size_t)cores > smith_verbs_jobs_max ? smith_verbs_jobs_max : (size_t)cores;
}

// True when `.clang-format-ignore` names the file (the one list of files that are test inputs, generated or copied, as
// smith fmt reads it: a pattern per line, `#` comments, matched against the path from the project's root).
static bool smith_lint_ignored(const char *name) {
    static char text[1U << 16];
    static bool read;
    static size_t size;
    if (!read) {
        read = true;
        FILE *file = fopen(".clang-format-ignore", "r");
        size = file != nullptr ? fread(text, 1, sizeof text - 1, file) : 0;
        if (file != nullptr) fclose(file);
        text[size] = '\0';
    }
    for (size_t at = 0; at < size;) { // bounded by the text
        size_t length = strcspn(text + at, "\n");
        char pattern[PATH_MAX];
        if (text[at] != '#' && length > 0 && length < sizeof pattern) {
            memcpy(pattern, text + at, length);
            pattern[length] = '\0';
            if (fnmatch(pattern, name, 0) == 0) return true;
        }
        at += length + 1;
    }
    return false;
}

// The .smith sources git sees that the build does not compile (a library the build only declares, a script, a template)
// and `.clang-format-ignore` does not name: appended to `files`, which clang-tidy reads with the nearest compile of the
// database. `sources` counts every .smith source git sees. Returns the new count.
static size_t smith_lint_unbuilt(const char **files, size_t count, size_t *sources) {
    size_t listed = 0;
    if (!smith_fmt_list(&listed)) return count;
    size_t built = count;
    for (size_t i = 0; i < listed; i++) { // bounded by the files git sees
        const char *name = smith_fmt_files[i];
        if (!smith_ends_with(name, ".smith") || smith_lint_ignored(name)) continue;
        *sources += 1;
        bool known = false;
        for (size_t j = 0; j < built && !known; j++) // bounded by the compiles
            known = strcmp(files[j], name) == 0;
        if (known) continue;
        if (count >= smith_verbs_files_max) smith_die(smith_exit_usage, "too many files to lint", nullptr);
        files[count++] = name;
    }
    return count;
}

// Lints the files: those the cache has a result for replay it, the others run clang-tidy side by side with the flags of
// their compile (.smith/compile_commands.json) and are stored when it completes. The last line says how many were
// linted and how many of those ran. Returns the exit status.
static int smith_lint_run(SmithLintRun *run, size_t count, const char *self) {
    uint64_t began = smith_account_now_ms();
    bool cache = smith_lc_open();
    size_t misses = 0;
    size_t failed = 0;
    for (size_t i = 0; i < count; i++) { // bounded by the files
        int status = 0;
        if (cache) smith_lc_plan(i, run->files[i], run->flags[i]);
        if (cache && smith_lc_replay(i, &status)) {
            if (status != 0) failed++;
        } else {
            run->order[misses++] = i;
        }
    }
    smith_lint_argv[0] = (char *)self;
    smith_lint_argv[1] = (char *)"lint";
    smith_lint_argv[2] = (char *)"--quiet";
    smith_lint_argv[3] = (char *)"-p";
    smith_lint_argv[4] = (char *)".smith";
    failed += smith_verbs_parallel(misses, smith_verbs_jobs(), smith_lint_job, smith_lint_done, run);
    if (failed > 0) fprintf(stderr, "smith lint: clang-tidy failed on %zu of %zu files\n", failed, count);
    uint64_t took = smith_account_now_ms() - began;
    fprintf(stderr, "smith lint: linted %zu files (%zu run, %zu cached, %.1f s)\n", count, misses, count - misses,
            (double)took / 1000.0);
    return failed > 0 ? 1 : 0;
}

// `smith lint` with no arguments: builds what the project's compiles read (the generated headers), then runs clang-tidy
// on each of its own C-family sources, side by side, with the flags of the compile (.smith/compile_commands.json). A
// `.smith` source is linted as written (shim/tidy.cpp) by a smith whose clang has the token hook, the way `smith lint
// x.smith -- <flags>` does: those the build compiles with their own command, the others git sees (and
// `.clang-format-ignore` does not name) with the command of the nearest compile. The others (the stage0) leave them out
// and say so once, as `smith cc` refuses them. The last line says how many files were linted, and a project with
// `.smith` sources that lints none is an error.
static int smith_lint_project(int count, char **args, const char *self) {
    int quiet = smith_quiet_verb("lint", count, args, self, false);
    if (quiet >= 0) return quiet;
    SmithVerbArgs *split = smith_verb_split(count, args);
    const char *const targets[] = {"smith_needs", nullptr};
    int status = smith_verb_build(self, split, targets, false);
    if (status != 0) return status;
    const SmithInfo *info = smith_info_read("smith lint");
    static const char *files[smith_verbs_files_max + 1];
    static const char *flags[smith_verbs_files_max + 1]; // of each file's compile; the unbuilt ones have none
    static size_t order[smith_verbs_files_max + 1];
    size_t files_count = 0;
    size_t skipped = 0;
    size_t smith_sources = 0;
    for (size_t i = 0; i < info->compile_count; i++) {
        if (smith_ends_with(info->compiles[i], ".smith")) {
            smith_sources += 1;
#ifndef SMITH_HOOK // the condition under which smith_route_clang refuses a .smith input
            skipped += 1;
            continue;
#endif
        }
        if (files_count >= smith_verbs_files_max) smith_die(smith_exit_usage, "too many files to lint", nullptr);
        flags[files_count] = info->compile_flags[i];
        files[files_count++] = info->compiles[i];
    }
#ifdef SMITH_HOOK
    files_count = smith_lint_unbuilt(files, files_count, &smith_sources);
#endif
    if (skipped > 0) {
        fprintf(stderr, "smith lint: %zu .smith source(s) left out: this smith's clang has no token hook\n", skipped);
    }
    if (files_count == 0) {
        fprintf(stderr, "smith lint: the build compiles no C or C++ source of the project's own\n");
        fprintf(stderr, "smith lint: linted 0 files\n");
        return smith_sources > skipped ? 1 : 0; // .smith sources and nothing linted: the gate is empty
    }
    SmithLintRun run = {.files = files, .flags = flags, .order = order};
    return smith_lint_run(&run, files_count, self);
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
// (sanitizers, modules, frame pointers, pattern initialization) and would change the IR that is proved, and the
// optimization level (the prover compiles with -O0 whatever it is: a unit whose hot sources have -O2 and whose others
// have none would otherwise be proved in two groups, and a group cannot see the bodies of the other), and the
// diagnostics a module's compile turns into errors (-Werror=<name>: a .smith module has them and its main has not,
// which would split a program into two groups that cannot see each other's contracts).
static bool smith_prove_drops(const char *word, bool *skip_next) {
    if (*skip_next) {
        *skip_next = false;
        return true;
    }
    if (strcmp(word, "-MF") == 0 || strcmp(word, "-c") == 0 || strcmp(word, "-o") == 0) {
        *skip_next = true;
        return true;
    }
    static const char *const levels[] = {"-O0", "-O1", "-O2", "-O3", "-Os", "-Oz"};
    for (size_t i = 0; i < sizeof levels / sizeof levels[0]; i++) {
        if (strcmp(word, levels[i]) == 0) return true;
    }
    static const char *const prefixes[] = {
        "-MMD",
        "-fsanitize",
        "-fno-sanitize",
        "-fmodule",
        "-fno-omit-frame-pointer",
        "-ftrivial-auto-var-init",
        "-Werror=",
        "-DSMITH_PROVED=", // the proof file's hash is no part of what is proved (#325)
        "-fprofile-instr",
        "-fcoverage-mapping"}; // coverage counters are not part of the proof (#456)
    for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++) {
        if (strncmp(word, prefixes[i], strlen(prefixes[i])) == 0) return true;
    }
    return false;
}

// The sources `smith prove` reads: C files and .smith modules, not build.c or build.smith (which are programs that
// write the manifest).
static bool smith_proves(const char *source) {
    if (strcmp(source, "build.c") == 0 || strcmp(source, "build.smith") == 0) return false;
    return smith_ends_with(source, ".c") || smith_ends_with(source, ".smith");
}

static char *smith_prove_argv[2 * smith_verbs_lines_max + smith_verbs_words_max + 8];

// Room for one more argument of the prover, or an error naming the limit.
static void smith_prove_room(size_t used) {
    if (used + 2 >= sizeof smith_prove_argv / sizeof smith_prove_argv[0])
        smith_die(smith_exit_usage, "smith prove: too many arguments for one group of sources (limit 393216)", nullptr);
}

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
static size_t smith_prove_groups_run;     // the groups `smith prove` started, so that none can be said
static size_t smith_prove_groups_without; // ... and those that had none of the --function names (exit status 4)
static bool smith_prove_wants_function;   // some --function= is among the options

// The prover's own options of the command line (-v, --budget=, --emit-ir=, --no-cache, --function=), which every group
// gets. The groups write one file: the first empties it and the others add to it.
static const SmithVerbArgs *smith_prove_options;

static void smith_prove_pass_options(size_t *used) {
    static char append[PATH_MAX + 32];
    for (size_t i = 0; smith_prove_options != nullptr && i < smith_prove_options->own_count; i++) { // bounded by args
        const char *option = smith_prove_options->own[i];
        constexpr char emit[] = "--emit-ir=";
        if (strncmp(option, emit, sizeof emit - 1) == 0 && smith_prove_groups_run > 1) {
            snprintf(append, sizeof append, "--emit-ir-append=%s", option + sizeof emit - 1);
            option = append;
        }
        smith_prove_room(*used);
        smith_prove_argv[(*used)++] = (char *)option;
    }
}

// The program's library sources, each as `--library=<source>`, after the files: smith's own code that the proof reads
// for its contracts and does not prove.
static void smith_prove_libraries(const char *libraries, size_t *used) {
    static char copy[smith_verbs_info_max + 1];
    static char words[smith_verbs_info_max + 16 * smith_verbs_lines_max];
    snprintf(copy, sizeof copy, "%s", libraries);
    size_t text = 0;
    char *source = copy;
    for (size_t guard = 0; *source != '\0' && guard < smith_verbs_info_max; guard++) { // bounded by the bytes
        char *tab = strchr(source, '\t');
        if (tab != nullptr) *tab = '\0';
        smith_prove_room(*used);
        SMITH_ASSERT(text + strlen(source) + 12 <= sizeof words);
        smith_prove_argv[(*used)++] = words + text;
        text += (size_t)snprintf(words + text, sizeof words - text, "--library=%s", source) + 1;
        if (tab == nullptr) break;
        source = tab + 1;
    }
}

// The words a `.smith` compile starts with, which a C file's do not (smith_lang_flags in the build runtime): sources
// whose flags differ by them alone are proved together, so a C file that calls into a module of the same program
// (smith.c into smith::random) finds the module's body, as it would linked. The group keeps the first member's flags,
// and a module that needs the folder gets it last, where it never shadows a header the first member's own -I paths
// find.
constexpr char smith_prove_lang_prefix[] = "-I\t.smith/include\t";

static const char *smith_prove_flags_core(const char *flags) {
    return strncmp(flags, smith_prove_lang_prefix, sizeof smith_prove_lang_prefix - 1) == 0
               ? flags + sizeof smith_prove_lang_prefix - 1
               : flags;
}

static bool smith_prove_flags_alike(const char *one, const char *other) {
    return strcmp(smith_prove_flags_core(one), smith_prove_flags_core(other)) == 0;
}

static int smith_prove_group(const char *self, const SmithInfo *info, const size_t *members, size_t member_count,
                             const char *libraries) {
    smith_prove_groups_run++;
    size_t used = 0;
    smith_prove_argv[used++] = (char *)self;
    smith_prove_argv[used++] = (char *)"prove";
    smith_prove_argv[used++] = (char *)"--in-project";
    smith_prove_pass_options(&used);
    for (size_t i = 0; i < member_count; i++)
        smith_prove_argv[used++] = (char *)info->compiles[members[i]];
    smith_prove_libraries(libraries, &used);
    smith_prove_argv[used++] = (char *)"--";
    // A copy: the compile's flags are shared by every group that proves the same source (a unity file), and cutting
    // them at their tabs in place would leave the next group with the first word only.
    static char flags_copy[smith_verbs_info_max + 1];
    int length = snprintf(flags_copy, sizeof flags_copy, "%s", info->compile_flags[members[0]]);
    bool needs_folder = false;
    for (size_t i = 0; i < member_count; i++)
        needs_folder =
            needs_folder || smith_prove_flags_core(info->compile_flags[members[i]]) != info->compile_flags[members[i]];
    if (needs_folder && smith_prove_flags_core(flags_copy) == flags_copy && length > 0 &&
        (size_t)length + sizeof smith_prove_lang_prefix < sizeof flags_copy)
        snprintf(flags_copy + length, sizeof flags_copy - (size_t)length, "\t-I\t.smith/include");
    char *flags = flags_copy;
    for (size_t guard = 0; *flags != '\0' && guard < smith_verbs_words_max; guard++) { // bounded by the words
        char *tab = strchr(flags, '\t');
        if (tab != nullptr) *tab = '\0';
        smith_prove_room(used);
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
static int smith_prove_unit(const char *self, const SmithInfo *info, char *sources, const char *libraries) {
    static size_t found[smith_verbs_lines_max];
    size_t found_count = 0;
    for (char *source = sources; source != nullptr && *source != '\0';) {
        char *tab = strchr(source, '\t');
        if (tab != nullptr) *tab = '\0';
        size_t at = smith_prove_compile_of(info, source);
        if (at < smith_verbs_lines_max && smith_proves(source)) {
            if (found_count >= smith_verbs_lines_max)
                smith_info_too_many("sources in one program", smith_verbs_lines_max);
            found[found_count++] = at;
        }
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
            if (done[i] || !smith_prove_flags_alike(info->compile_flags[found[i]], info->compile_flags[found[first]]))
                continue;
            done[i] = true;
            members[member_count++] = found[i];
        }
        int one = smith_prove_group(self, info, members, member_count, libraries);
        if (one == 4) { // none of the --function names is defined in this group: another has it, or none does
            smith_prove_groups_without++;
            one = 0;
        }
        if (one != 0) status = one;
    }
    return status;
}

// `smith prove` with no arguments: the contracts of the sources each program of the build links (not the tests'
// runner), each compiled with the flags the build gives it, less what only instruments or caches (sanitizers and
// modules). Sources with the same flags are proved together; programs with the same sources once.
static int smith_prove_project(int count, char **args, const char *self) {
    int quiet = smith_quiet_verb("prove", count, args, self, false);
    if (quiet >= 0) return quiet;
    if (!smith_has_prover()) {
        smith_die(smith_exit_usage, "this smith is the stage0, which has no prover; the full smith has it",
                  "smith build llvm, then smith build");
    }
    SmithVerbArgs *split = smith_verb_split(count, args);
    smith_prove_options = split;
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
    for (size_t i = 0; i < split->own_count; i++)
        smith_prove_wants_function = smith_prove_wants_function || strncmp(split->own[i], "--function=", 11) == 0;
    for (size_t i = 0; i < info->unit_count; i++) {
        if (repeated[i]) continue;
        int one =
            smith_prove_unit(self, info, (char *)info->units[i], i < info->library_count ? info->libraries[i] : "");
        if (one != 0) status = one;
    }
    if (status == 0 && smith_prove_wants_function && smith_prove_groups_without == smith_prove_groups_run &&
        smith_prove_groups_run > 0) {
        fprintf(stderr,
                "smith prove: --function: no function by that name in any source the project proves; if the source "
                "defines it, it is a static function that nothing calls, which clang does not emit (mark it "
                "[[gnu::used]] to keep it)\n");
        return 1;
    }
    if (status == 0 && smith_prove_groups_run == 0) {
        printf("smith: prove: nothing to prove: no function with smith_requires, smith_ensures, smith_invariant or "
               "smith_assert\n0 of 0 functions have contracts\n");
    }
    return status;
}

// ---- test and run

// `-k` and `-k N` (ninja's: how many failed jobs to put up with, 0 for any number; a bare `-k` is `-k 0`) are the
// build's, not the runner's: `smith test -k` compiles every test file and reports every compile error in one run.
static void smith_verb_take_keep_going(SmithVerbArgs *split) {
    static char keep_going[32];
    size_t kept = 0;
    for (size_t i = 0; i < split->own_count; i++) { // bounded by smith_build_args_max
        char *arg = split->own[i];
        if (strncmp(arg, "-k", 2) != 0 || strlen(arg) >= sizeof keep_going - 3) {
            split->own[kept++] = arg;
            continue;
        }
        const char *value = arg + 2;
        bool next =
            arg[2] == '\0' && i + 1 < split->own_count && split->own[i + 1][0] >= '0' && split->own[i + 1][0] <= '9';
        if (next) value = split->own[++i];
        snprintf(keep_going, sizeof keep_going, "-k%s", value[0] != '\0' ? value : "0");
        if (split->option_count < smith_build_args_max) split->options[split->option_count++] = keep_going;
    }
    split->own_count = kept;
}

// Whether `--changed` is among the arguments, so `--list` is the selection's and not the runner's.
static bool smith_test_wants_list(const SmithVerbArgs *split) {
    for (size_t i = 0; i < split->own_count; i++) {
        if (strcmp(split->own[i], "--changed") == 0) return true;
    }
    return false;
}

// `--list`: the selected tests on stdout, one per line (a name, or `module::` for a module's tests; `*` when every
// test runs), and nothing run. The report of why goes to stderr.
static int smith_test_print_selection(SmithChangedResult result, const char *filter) {
    size_t count = 0;
    for (const char *at = filter; result == SmithChanged_Some && *at != '\0'; at++) // bounded by the filter's text
        count += (at == filter || at[-1] == ',') ? 1 : 0;
    // One line on stderr, with or without -v: what the selection is.
    fprintf(stderr, "smith test --changed: %s\n",
            result == SmithChanged_Full   ? "selects every test (*)"
            : result == SmithChanged_None ? "selects no test"
                                          : "selects the tests listed on stdout");
    if (result == SmithChanged_Some) fprintf(stderr, "smith test --changed: %zu entries\n", count);
    if (result == SmithChanged_Full) {
        puts("*");
    } else if (result == SmithChanged_Some) {
        for (const char *at = filter; *at != '\0';) { // bounded by the filter's text
            size_t length = strcspn(at, ",");
            if (length > 0) printf("%.*s\n", (int)length, at);
            at += length + (at[length] == ',' ? 1 : 0);
        }
    }
    return 0;
}

// `--changed [--base <ref>] [--dry-run | --list [<base> [<head>]]]` (runtime/changed.c): takes them out of the
// arguments and, unless the whole suite runs, adds `--test <names>` for the tests the change can affect. Returns an
// exit status when there is nothing to run (nothing selected, a dry run, or a failure), else -1.
typedef struct {
    const char *base;
    const char *head;
    bool changed;
    bool dry_run;
    bool list;
    bool cheapest;
} SmithTestChangedArgs;

// Takes the `--changed` family out of the arguments of `smith test`; the rest stays for the runner.
static SmithTestChangedArgs smith_test_changed_args(SmithVerbArgs *split) {
    SmithTestChangedArgs found = {};
    size_t kept = 0;
    for (size_t i = 0; i < split->own_count; i++) { // bounded by smith_build_args_max
        char *arg = split->own[i];
        if (strcmp(arg, "--changed") == 0) {
            found.changed = true;
        } else if (strcmp(arg, "--cheapest-first") == 0) {
            found.cheapest = true;
        } else if (strcmp(arg, "--dry-run") == 0) {
            found.dry_run = true;
        } else if (strcmp(arg, "--list") == 0 && smith_test_wants_list(split)) {
            found.list = true;
            for (size_t refs = 0; refs < 2 && i + 1 < split->own_count && split->own[i + 1][0] != '-'; refs++) {
                if (found.base == nullptr)
                    found.base = split->own[++i];
                else
                    found.head = split->own[++i];
            }
        } else if (strcmp(arg, "--base") == 0 && i + 1 < split->own_count) {
            found.base = split->own[++i];
        } else {
            split->own[kept++] = arg;
        }
    }
    split->own_count = kept;
    return found;
}

static int smith_test_take_changed(SmithVerbArgs *split, const char *self) {
    SmithChangedBuffer selection = {};
    SmithTestChangedArgs given = smith_test_changed_args(split);
    const char *base = given.base;
    const char *head = given.head;
    bool changed = given.changed;
    bool dry_run = given.dry_run;
    bool list = given.list;
    bool cheapest = given.cheapest;
    if (!changed) {
        if (cheapest) smith_die(smith_exit_usage, "--cheapest-first goes with --changed", "test");
        if (dry_run || base != nullptr) smith_die(smith_exit_usage, "--dry-run and --base go with --changed", "test");
        return -1;
    }
    smith_changed_self = self;
    SmithChangedResult result = smith_changed_select(base, head, &selection);
    if (result == SmithChanged_Failed) return smith_exit_usage;
    const char *filter = smith_changed_buffer_text(&selection);
    if (list) return smith_test_print_selection(result, filter);
    // The selection is one argument of the runner; the arguments of a verb travel to build.c in 16384 bytes
    // (smith_verb_args_max), so a longer one runs everything, and says so (never silently).
    if (result == SmithChanged_Some && selection.used > smith_changed_argument_max) {
        fprintf(stderr,
                "smith test --changed: the selection (%zu bytes) is longer than one argument, so every test runs\n",
                selection.used);
        result = SmithChanged_Full;
    }
    static char costs[PATH_MAX + 16];
    if (cheapest && smith_changed_write_costs(costs, sizeof costs) && split->own_count + 2 <= smith_build_args_max) {
        split->own[split->own_count++] = (char *)"--costs";
        split->own[split->own_count++] = costs;
    }
    if (result == SmithChanged_None) {
        fputs("smith test --changed: no test is affected\n", stderr);
        return 0;
    }
    if (result == SmithChanged_Some && split->own_count + 2 <= smith_build_args_max) {
        split->own[split->own_count++] = (char *)"--test";
        split->own[split->own_count++] = (char *)filter;
    }
    if (dry_run) {
        fprintf(stderr, "smith test --changed: %s\n",
                result == SmithChanged_Some ? "--dry-run: the tests run with --test (above)"
                                            : "--dry-run: every test runs");
        if (result == SmithChanged_Some) fprintf(stderr, "smith test --changed: --test %s\n", filter);
        return 0;
    }
    // The tests it runs count the lines they run and leave a record each (issue #456): a build option, not the
    // environment, so the smiths the tests start are not instrumented.
    static char record[] = "-DSMITH_RECORD=1";
    if (split->option_count < smith_build_args_max) split->options[split->option_count++] = record;
    return -1;
}

// `smith test`: builds and runs the tests smith_tests declares; the arguments go to the runner.
static int smith_test_project(int count, char **args, const char *self) {
    if (count > 0 && strcmp(args[0], "--coverage") == 0) return smith_coverage_main(count - 1, args + 1); // #456
    int quiet = smith_quiet_verb("test", count, args, self, true);
    if (quiet >= 0) return quiet;
    SmithVerbArgs *split = smith_verb_split(count, args);
    for (size_t i = 0; i < split->own_count; i++) { // -v is the runner's --verbose
        if (strcmp(split->own[i], "-v") == 0) split->own[i] = (char *)"--verbose";
    }
    smith_verb_take_keep_going(split);
    int changed = smith_test_take_changed(split, self);
    if (changed >= 0) return changed;
    const char *const targets[] = {"smith_tests", nullptr};
    // Each task's wall and CPU time and smith starts (runtime/account.c), unless SMITH_ACCOUNT=no; =all lists every
    // task.
    const char *mode = getenv("SMITH_ACCOUNT");
    bool accounted = mode == nullptr || strcmp(mode, "no") != 0;
    char account[PATH_MAX];
    snprintf(account, sizeof account, "%s/smith-account.%d", getenv("TMPDIR") != nullptr ? getenv("TMPDIR") : "/tmp",
             (int)getpid());
    (void)unlink(account);
    if (accounted) setenv("SMITH_ACCOUNT_FILE", account, 1);
    int status = smith_verb_build(self, split, targets, true);
    if (accounted) {
        smith_account_report(account, "smith test", mode != nullptr && strcmp(mode, "all") == 0);
        (void)unlink(account);
    }
    if (status != 0) return status;
    if (smith_info_read("smith test")->test_count == 0) {
        fprintf(stderr, "smith test: the build declares no tests; smith_tests (<smith/build.h>) declares them\n");
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
    static const char *named[smith_verbs_lines_max];
    size_t named_count = 0;
    for (size_t i = 0; i < info->default_count; i++) {
        for (size_t j = 0; j < info->program_count; j++) {
            if (strcmp(info->defaults[i], info->programs[j]) == 0) named[named_count++] = info->defaults[i];
        }
    }
    if (named_count != 1) {
        fprintf(stderr, "smith run: the default of %s names %s program%s", smith_project_build_file(),
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

// Reports a part: its line, and for a part whose output went to a log, the detail on success and the excerpt on
// failure.
static void smith_check_part(const char *name, int status, SmithCheckClock since, const char **failed,
                             size_t *failed_count, const char *log) {
    SmithCheckClock now = smith_check_clock();
    double wall = (double)(now.wall_ms - since.wall_ms) / 1000.0;
    double cpu = (double)(now.cpu_ms - since.cpu_ms) / 1000.0;
    size_t size = 0;
    const char *text = log != nullptr ? smith_quiet_read(log, &size) : "";
    if (status == 0) {
        char detail[128], coverage[64];
        smith_quiet_detail(name, text, size, detail, sizeof detail, coverage, sizeof coverage);
        char shown[80] = "";
        if (coverage[0] != '\0') snprintf(shown, sizeof shown, " (%s)", coverage);
        fprintf(stderr, "smith check: %s ok%s%s%s (%.1f s wall, %.1f s cpu)\n", name, shown,
                detail[0] != '\0' ? ": " : "", detail, wall, cpu);
        return;
    }
    fprintf(stderr, "smith check: %s FAILED (status %d) (%.1f s wall, %.1f s cpu)\n", name, status, wall, cpu);
    if (log != nullptr) smith_quiet_failure("smith check", log, text, size);
    failed[(*failed_count)++] = name;
}

// Runs `smith <verb>` with the options of the check: its output goes to `.smith/log/<verb>.log` unless the check is
// loud, when it is the check's own.
static int smith_check_child(const char *self, const char *verb, const SmithVerbArgs *split, bool loud, char *log) {
    if (!loud) {
        smith_quiet_log_path(log, PATH_MAX, verb);
        return smith_quiet_child(self, verb, (int)split->option_count, split->options, log);
    }
    static char *argv[smith_build_args_max + 3];
    size_t used = 0;
    argv[used++] = (char *)self;
    argv[used++] = (char *)verb;
    for (size_t i = 0; i < split->option_count; i++)
        argv[used++] = split->options[i];
    argv[used] = nullptr;
    return smith_verbs_wait(smith_verbs_start(argv, nullptr));
}

// One part of the check that runs as a child, timed and reported.
static void smith_check_run(const char *self, const char *verb, const SmithVerbArgs *split, bool loud,
                            const char **failed, size_t *failed_count) {
    char log[PATH_MAX];
    SmithCheckClock since = smith_check_clock();
    int status = smith_check_child(self, verb, split, loud, log);
    smith_check_part(verb, status, since, failed, failed_count, loud ? nullptr : log);
}

// ---- promises

// What smith trusts and does not check: the `P` lines of the effects summaries under `.smith/effects/` (an effects
// attribute on a declaration with no source; a function that calls `smith_trusted()`). A header's promise is in the
// summary of every file that includes it, so one is listed once, by place and name.
constexpr size_t smith_promises_max = 1024;
constexpr size_t smith_promise_text = 192;
static char smith_promise_list[smith_promises_max][smith_promise_text];
static size_t smith_promise_count;

static void smith_promise_add(const char *place, const char *name) {
    char entry[smith_promise_text];
    if (strstr(place, "/.smith/") != nullptr || strncmp(place, ".smith/", 7) == 0)
        return;                             // smith's own generated files
    const char *last = strrchr(place, ':'); // the column goes: `file:line:column`
    int width = last != nullptr ? (int)(last - place) : (int)strlen(place);
    snprintf(entry, sizeof entry, "%.*s %s", width, place, name);
    for (size_t i = 0; i < smith_promise_count; i++) {
        if (strcmp(smith_promise_list[i], entry) == 0) return;
    }
    if (smith_promise_count < smith_promises_max) strcpy(smith_promise_list[smith_promise_count++], entry);
}

static void smith_promise_read(const char *path) {
    FILE *file = fopen(path, "r");
    if (file == nullptr) return;
    char line[2048];
    while (fgets(line, sizeof line, file) != nullptr) { // bounded by the file
        char *field[5] = {};
        size_t found = 0;
        for (char *c = line; found < 5 && c != nullptr;) { // P, kind, symbol, place, name
            field[found++] = c;
            c = strchr(c, '\t');
            if (c != nullptr) *c++ = '\0';
        }
        if (found == 5 && strcmp(field[0], "P") == 0) {
            field[4][strcspn(field[4], "\n")] = '\0';
            smith_promise_add(field[3], field[4]);
        }
    }
    fclose(file);
}

static int smith_promise_visit(const char *path, const struct stat *info, int type, struct FTW *where) {
    (void)info;
    (void)where;
    size_t length = strlen(path);
    if (type == FTW_F && length > 3 && strcmp(path + length - 3, ".fx") == 0) smith_promise_read(path);
    return 0;
}

// `smith check: promises: N`, and with -v each one (`file:line name`). Nothing when the project has none.
static void smith_check_promises(bool list) {
    smith_promise_count = 0;
    nftw(".smith/effects", smith_promise_visit, 16, FTW_PHYS);
    if (smith_promise_count == 0) return;
    fprintf(stderr, "smith check: promises: %zu%s\n", smith_promise_count, list ? "" : " (smith check -v lists them)");
    for (size_t i = 0; list && i < smith_promise_count; i++)
        fprintf(stderr, "smith check:   %s\n", smith_promise_list[i]);
}

// `smith check`: formatting (clang-format's dry run), the docs, lint, the tests and the proofs, in that order, cheapest
// first. A part that fails does not stop the rest: every part is reported, and the exit status is 1 if any failed.
static int smith_check_project(int count, char **args, const char *self) {
    SmithVerbArgs *split = smith_verb_split(count, args);
    bool every_task = split->own_count == 1 && strcmp(split->own[0], "-v") == 0;
    if (split->own_count > (every_task ? 1U : 0U))
        smith_die(smith_exit_usage, "takes no arguments but -D options and -v", "check");
    if (every_task) setenv("SMITH_ACCOUNT", "all", 1); // `smith test` lists every task, not the slowest few
    bool loud = every_task || smith_quiet_full();      // otherwise each part's output goes to its log
    SmithCheckClock since = {};
    const char *failed[8];
    size_t failed_count = 0;
    since = smith_check_clock();
    smith_check_part("fmt", smith_fmt_project(self, true), since, failed, &failed_count, nullptr);
    since = smith_check_clock();
    smith_check_part("docs", smith_docs_check(0, nullptr), since, failed, &failed_count, nullptr);
    if (access("build.c", R_OK) != 0 && access("build.smith", R_OK) != 0 && access("main.smith", R_OK) != 0 &&
        access("main.c", R_OK) != 0) {
        fprintf(stderr, "smith check: no build file and no main, so lint, test and prove are skipped\n");
    } else {
        smith_check_run(self, "lint", split, loud, failed, &failed_count);
        smith_check_run(self, "test", split, loud, failed, &failed_count);
        if (smith_has_prover()) {
            smith_check_run(self, "prove", split, loud, failed, &failed_count);
        } else {
            fprintf(stderr, "smith check: this smith has no prover, so prove is skipped\n");
        }
        // The promises are read from the summaries of the project's own compiles, which lint, test and prove need not
        // have made (a project never built): the default build, a no-op when fresh, makes them.
        char build_log[PATH_MAX];
        (void)smith_check_child(self, "build", split, loud, build_log);
    }
    smith_check_promises(every_task);
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

// `smith fmt` with no file names: in place, or with --dry-run (-n) the project's check, which fails on a file that
// would change (--Werror is always part of it).
static int smith_fmt_verb(int count, char **args, const char *self) {
    SmithVerbArgs *split = smith_verb_split(count, args);
    bool check = false;
    for (size_t i = 0; i < split->own_count; i++)
        check = check || strcmp(split->own[i], "--dry-run") == 0 || strcmp(split->own[i], "-n") == 0;
    return smith_fmt_project(self, check);
}
