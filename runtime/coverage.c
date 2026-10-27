// Coverage-based test selection (issue #456, knowledge/coverage-selection.md). Included by smith.c, before
// runtime/changed.c. The train's full run builds smith with `-fprofile-instr-generate -fcoverage-mapping`; the test
// runner gives each test a profile folder (LLVM_PROFILE_FILE), and after the test runs
// `smith test --coverage merge <test>` merges its profiles and reads them against the programs (shim/coverage.cpp, as
// llvm-profdata and llvm-cov export -format=lcov would) and keeps one small record: the functions the test executed, as
// a bit for each function of the binary, and the lines it covered. `smith test --coverage join` makes one table of the
// records; `report` names the changed lines no test ran. `smith test --changed` reads the table (runtime/changed.c).
//
// The table is text, one line each, fields separated by a tab:
//   smith-coverage 1                          the first line
//   commit <sha>                              the commit the run checked
//   fn <file> <first line> <last line> <name> a function of the binary; its place in this list is its bit
//   inst <file> <ranges>                      the lines coverage counts ("3-9,12")
//   cov <file> <ranges>                       the lines some test ran
//   test <name> <hex>                         the bits of the functions the test executed
static bool smith_changed_git(const char *const *arguments, void (*line)(char *)); // runtime/changed.c

static bool smith_rec_write(const char *dir, const char *name, const char *clean, bool pass); // runtime/record.c

constexpr size_t smith_cov_files_max = 1024;
constexpr size_t smith_cov_functions_max = 1U << 16;
constexpr size_t smith_cov_tests_max = 6144;
constexpr size_t smith_cov_lines_max = 1U << 15;
constexpr size_t smith_cov_pool_max = 1U << 23;
constexpr size_t smith_cov_text_max = 1U << 25;
constexpr size_t smith_cov_bits_bytes = smith_cov_functions_max / 8;
constexpr size_t smith_cov_line_bytes = smith_cov_lines_max / 8;

typedef struct {
    uint32_t file;
    uint32_t first;
    uint32_t last;
    const char *name;
} SmithCovFunction;

static char smith_cov_pool[smith_cov_pool_max];
static size_t smith_cov_pool_used;
static const char *smith_cov_files[smith_cov_files_max];
static size_t smith_cov_file_count;
static uint8_t smith_cov_inst[smith_cov_files_max][smith_cov_line_bytes];
static uint8_t smith_cov_cover[smith_cov_files_max][smith_cov_line_bytes];
static SmithCovFunction smith_cov_functions[smith_cov_functions_max];
static size_t smith_cov_function_count;
static bool smith_cov_function_run[smith_cov_functions_max]; // executed in the lcov being read
static const char *smith_cov_test_names[smith_cov_tests_max];
static uint8_t smith_cov_test_bits[smith_cov_tests_max][smith_cov_bits_bytes];
static size_t smith_cov_test_count;
static char smith_cov_commit[64];
static char smith_cov_text[smith_cov_text_max + 1];
static bool smith_cov_overflow;

static const char *smith_cov_copy(const char *text) {
    size_t length = strlen(text);
    if (smith_cov_pool_used + length + 1 > smith_cov_pool_max) {
        smith_cov_overflow = true;
        return "";
    }
    char *at = smith_cov_pool + smith_cov_pool_used;
    memcpy(at, text, length + 1);
    smith_cov_pool_used += length + 1;
    return at;
}

static bool smith_cov_get(const uint8_t *bits, size_t at) {
    return (((unsigned)bits[at / 8] >> (at % 8)) & 1U) != 0;
}

static void smith_cov_set(uint8_t *bits, size_t at) {
    bits[at / 8] = (uint8_t)(bits[at / 8] | (1U << (at % 8)));
}

static int smith_cov_file_find(const char *path) {
    for (size_t i = 0; i < smith_cov_file_count; i++) { // bounded by the files
        if (strcmp(smith_cov_files[i], path) == 0) return (int)i;
    }
    return -1;
}

static int smith_cov_file_index(const char *path) {
    int at = smith_cov_file_find(path);
    if (at >= 0) return at;
    if (smith_cov_file_count >= smith_cov_files_max) {
        smith_cov_overflow = true;
        return -1;
    }
    smith_cov_files[smith_cov_file_count] = smith_cov_copy(path);
    return (int)smith_cov_file_count++;
}

// "3-9,12" into the bits of `bits`.
static void smith_cov_ranges_read(const char *text, uint8_t *bits) {
    const char *at = text;
    for (size_t guard = 0; *at != '\0' && guard < smith_cov_lines_max; guard++) { // bounded by the lines
        char *end = nullptr;
        unsigned long first = strtoul(at, &end, 10);
        unsigned long last = first;
        if (end == at) return;
        if (*end == '-') last = strtoul(end + 1, &end, 10);
        for (unsigned long line = first; line <= last && line < smith_cov_lines_max; line++)
            smith_cov_set(bits, line);
        at = *end == ',' ? end + 1 : end;
    }
}

static void smith_cov_ranges_write(FILE *out, const uint8_t *bits) {
    bool first = true;
    for (size_t line = 0; line < smith_cov_lines_max; line++) { // bounded by the lines
        if (!smith_cov_get(bits, line)) continue;
        size_t last = line;
        while (last + 1 < smith_cov_lines_max && smith_cov_get(bits, last + 1))
            last++;
        fprintf(out, last == line ? "%s%zu" : "%s%zu-", first ? "" : ",", line);
        if (last != line) fprintf(out, "%zu", last);
        first = false;
        line = last;
    }
}

static bool smith_cov_any(const uint8_t *bits, size_t bytes) {
    for (size_t i = 0; i < bytes; i++) { // bounded by the bitmap
        if (bits[i] != 0) return true;
    }
    return false;
}

static size_t smith_cov_slurp(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return 0;
    size_t size = fread(smith_cov_text, 1, smith_cov_text_max, file);
    fclose(file);
    smith_cov_text[size] = '\0';
    return size;
}

static size_t smith_cov_hex_bytes(void) {
    return (smith_cov_function_count + 7) / 8;
}

static void smith_cov_bits_read(const char *hex, uint8_t *bits) {
    for (size_t i = 0; i < smith_cov_bits_bytes && hex[2 * i] != '\0' && hex[2 * i + 1] != '\0'; i++) {
        char pair[3] = {hex[2 * i], hex[2 * i + 1], '\0'};
        bits[i] = (uint8_t)strtoul(pair, nullptr, 16);
    }
}

// One line of a table (the tabs already cut to NUL: `field[0]` is the kind).
static void smith_cov_table_line(char *const *field, size_t count) {
    if (count >= 2 && strcmp(field[0], "commit") == 0) {
        snprintf(smith_cov_commit, sizeof smith_cov_commit, "%s", field[1]);
    } else if (count >= 5 && strcmp(field[0], "fn") == 0 && smith_cov_function_count < smith_cov_functions_max) {
        int file = smith_cov_file_index(field[1]);
        if (file < 0) return;
        smith_cov_functions[smith_cov_function_count++] =
            (SmithCovFunction){(uint32_t)file, (uint32_t)strtoul(field[2], nullptr, 10),
                               (uint32_t)strtoul(field[3], nullptr, 10), smith_cov_copy(field[4])};
    } else if (count >= 3 && (strcmp(field[0], "inst") == 0 || strcmp(field[0], "cov") == 0)) {
        int file = smith_cov_file_index(field[1]);
        if (file >= 0)
            smith_cov_ranges_read(field[2], field[0][0] == 'i' ? smith_cov_inst[file] : smith_cov_cover[file]);
    } else if (count >= 3 && strcmp(field[0], "test") == 0 && smith_cov_test_count < smith_cov_tests_max) {
        smith_cov_test_names[smith_cov_test_count] = smith_cov_copy(field[1]);
        smith_cov_bits_read(field[2], smith_cov_test_bits[smith_cov_test_count++]);
    }
}

// Reads a table, or a record, into the arrays; false when the file cannot be read or is not one.
static bool smith_cov_load(const char *path) {
    if (smith_cov_slurp(path) == 0 || strncmp(smith_cov_text, "smith-coverage 1\n", 17) != 0) return false;
    char *line = smith_cov_text + 17;
    for (size_t guard = 0; *line != '\0' && guard < (size_t)1 << 24; guard++) { // bounded by the lines
        char *end = strchr(line, '\n');
        if (end != nullptr) *end = '\0';
        char *field[6] = {};
        size_t count = 0;
        for (char *at = line; count < 5 && at != nullptr;) { // the name is the last field and may hold spaces
            field[count++] = at;
            at = strchr(at, '\t');
            if (at != nullptr) *at++ = '\0';
        }
        smith_cov_table_line(field, count);
        if (end == nullptr) break;
        line = end + 1;
    }
    return !smith_cov_overflow;
}

// ---- Reading llvm-cov's lcov

static char smith_cov_root[PATH_MAX];

// `path` relative to the repository, or nullptr outside it (the SDK, LLVM's headers) or in `.smith/`.
static const char *smith_cov_relative(const char *path) {
    size_t root = strlen(smith_cov_root);
    if (strncmp(path, smith_cov_root, root) != 0 || path[root] != '/') return nullptr;
    const char *relative = path + root + 1;
    return strncmp(relative, ".smith/", 7) == 0 || strncmp(relative, "build/", 6) == 0 ? nullptr : relative;
}

// The last line a function covers: before the next function that starts later in its file, and not past the last line
// coverage counts there.
static void smith_cov_ends(size_t from, size_t to, int file) {
    for (size_t f = from; f < to; f++) { // bounded by the functions of the file
        uint32_t limit = smith_cov_lines_max - 1;
        for (size_t g = from; g < to; g++) {
            if (smith_cov_functions[g].first > smith_cov_functions[f].first && smith_cov_functions[g].first - 1 < limit)
                limit = smith_cov_functions[g].first - 1;
        }
        uint32_t last = smith_cov_functions[f].first;
        for (uint32_t line = smith_cov_functions[f].first; line <= limit; line++) {
            if (smith_cov_get(smith_cov_inst[file], line)) last = line;
        }
        smith_cov_functions[f].last = last;
    }
}

// The functions of one file by first line, then name, so a function's bit does not depend on llvm-cov's order.
static void smith_cov_sort(size_t from, size_t to) {
    for (size_t i = from + 1; i < to; i++) { // an insertion sort, bounded by the functions of the file
        for (size_t j = i; j > from; j--) {
            const SmithCovFunction *a = &smith_cov_functions[j - 1];
            const SmithCovFunction *b = &smith_cov_functions[j];
            if (a->first < b->first || (a->first == b->first && strcmp(a->name, b->name) <= 0)) break;
            SmithCovFunction swap = smith_cov_functions[j - 1];
            smith_cov_functions[j - 1] = smith_cov_functions[j];
            smith_cov_functions[j] = swap;
            bool run = smith_cov_function_run[j - 1];
            smith_cov_function_run[j - 1] = smith_cov_function_run[j];
            smith_cov_function_run[j] = run;
        }
    }
}

typedef struct {
    int file;
    size_t first_function;
} SmithCovRecord;

static void smith_cov_lcov_line(char *line, SmithCovRecord *record) {
    if (strncmp(line, "SF:", 3) == 0) {
        const char *relative = smith_cov_relative(line + 3);
        record->file = relative != nullptr ? smith_cov_file_index(relative) : -1;
        record->first_function = smith_cov_function_count;
    } else if (record->file < 0) {
        return;
    } else if (strncmp(line, "FN:", 3) == 0 && smith_cov_function_count < smith_cov_functions_max) {
        char *comma = strchr(line + 3, ',');
        if (comma == nullptr) return;
        *comma = '\0';
        smith_cov_functions[smith_cov_function_count++] = (SmithCovFunction){
            (uint32_t)record->file, (uint32_t)strtoul(line + 3, nullptr, 10), 0, smith_cov_copy(comma + 1)};
    } else if (strncmp(line, "FNDA:", 5) == 0) {
        char *comma = strchr(line + 5, ',');
        if (comma == nullptr || strtoul(line + 5, nullptr, 10) == 0) return;
        for (size_t f = record->first_function; f < smith_cov_function_count; f++) { // the functions of this file
            if (strcmp(smith_cov_functions[f].name, comma + 1) == 0) smith_cov_function_run[f] = true;
        }
    } else if (strncmp(line, "DA:", 3) == 0) {
        char *comma = strchr(line + 3, ',');
        unsigned long number = strtoul(line + 3, nullptr, 10);
        if (comma == nullptr || number >= smith_cov_lines_max) return;
        smith_cov_set(smith_cov_inst[record->file], number);
        if (strtoul(comma + 1, nullptr, 10) > 0) smith_cov_set(smith_cov_cover[record->file], number);
    } else if (strcmp(line, "end_of_record") == 0) {
        smith_cov_sort(record->first_function, smith_cov_function_count);
        smith_cov_ends(record->first_function, smith_cov_function_count, record->file);
        record->file = -1;
    }
}

static bool smith_cov_lcov(const char *path) {
    if (smith_cov_slurp(path) == 0) return false;
    SmithCovRecord record = {.file = -1};
    char *line = smith_cov_text;
    for (size_t guard = 0; *line != '\0' && guard < (size_t)1 << 24; guard++) { // bounded by the lines
        char *end = strchr(line, '\n');
        if (end != nullptr) *end = '\0';
        smith_cov_lcov_line(line, &record);
        if (end == nullptr) break;
        line = end + 1;
    }
    return !smith_cov_overflow;
}

// ---- Writing

static void smith_cov_write_functions(FILE *out) {
    for (size_t f = 0; f < smith_cov_function_count; f++) { // bounded by the functions
        const SmithCovFunction *fn = &smith_cov_functions[f];
        fprintf(out, "fn\t%s\t%u\t%u\t%s\n", smith_cov_files[fn->file], fn->first, fn->last, fn->name);
    }
}

static void smith_cov_write_lines(FILE *out, const char *kind, uint8_t (*maps)[smith_cov_line_bytes]) {
    for (size_t i = 0; i < smith_cov_file_count; i++) { // bounded by the files
        if (!smith_cov_any(maps[i], smith_cov_line_bytes)) continue;
        fprintf(out, "%s\t%s\t", kind, smith_cov_files[i]);
        smith_cov_ranges_write(out, maps[i]);
        fputc('\n', out);
    }
}

static void smith_cov_write_test(FILE *out, size_t test) {
    fprintf(out, "test\t%s\t", smith_cov_test_names[test]);
    for (size_t i = 0; i < smith_cov_hex_bytes(); i++) // bounded by the functions
        fprintf(out, "%02x", smith_cov_test_bits[test][i]);
    fputc('\n', out);
}

// Writes through a temporary name and renames, so a reader never sees half a file.
static FILE *smith_cov_open(const char *path, char *temporary, size_t size) {
    snprintf(temporary, size, "%s.%d.tmp", path, (int)getpid());
    return fopen(temporary, "w");
}

static bool smith_cov_close(FILE *out, const char *temporary, const char *path) {
    bool good = fclose(out) == 0;
    if (good && rename(temporary, path) == 0) return true;
    (void)unlink(temporary);
    return false;
}

static void smith_cov_clean_name(const char *name, char *out, size_t size) {
    size_t used = 0;
    for (; name[used] != '\0' && used + 1 < size; used++) {
        char c = name[used];
        bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        out[used] = word ? c : '_';
    }
    out[used] = '\0';
}

// ---- merge, join and report

static const char *smith_cov_env(const char *name) {
    const char *value = getenv(name);
    return value != nullptr && value[0] != '\0' ? value : nullptr;
}

// Runs `argv`, with its output in `output` when given; its exit status, or -1.
static int smith_cov_spawn(const char *const *argv, const char *output, bool output_to_stderr) {
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (output_to_stderr) posix_spawn_file_actions_adddup2(&actions, STDERR_FILENO, STDOUT_FILENO);
    if (output != nullptr)
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, output, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    pid_t pid = 0;
    int started = posix_spawn(&pid, argv[0], &actions, nullptr, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    int status = 0;
    if (started != 0 || waitpid(pid, &status, 0) != pid) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Deletes the raw profiles of a test and the folder; writes their names to `list`. Returns how many there were.
static size_t smith_cov_raw_list(const char *folder, const char *list, bool remove_them) {
    DIR *dir = opendir(folder);
    if (dir == nullptr) return 0;
    FILE *out = remove_them ? nullptr : fopen(list, "w");
    size_t count = 0;
    struct dirent *entry = nullptr;
    for (size_t guard = 0; (entry = readdir(dir)) != nullptr && guard < (size_t)1 << 20; guard++) { // the folder
        if (strstr(entry->d_name, ".profraw") == nullptr) continue;
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", folder, entry->d_name);
        if (out != nullptr) fprintf(out, "%s\n", path);
        if (remove_them) (void)unlink(path);
        count++;
    }
    closedir(dir);
    if (out != nullptr) fclose(out);
    return count;
}

static bool smith_cov_write_all(const char *dir) {
    char path[PATH_MAX];
    char temporary[PATH_MAX + 32];
    snprintf(path, sizeof path, "%s/all.tsv", dir);
    if (access(path, R_OK) == 0) return true;
    FILE *out = smith_cov_open(path, temporary, sizeof temporary);
    if (out == nullptr) return false;
    fputs("smith-coverage 1\n", out);
    smith_cov_write_functions(out);
    smith_cov_write_lines(out, "inst", smith_cov_inst);
    return smith_cov_close(out, temporary, path);
}

// The record of one test: <dir>/rec/<clean>.rec, and <dir>/all.tsv if no test made it yet.
static int smith_cov_merge_record(const char *dir, const char *name, const char *clean) {
    char record[PATH_MAX];
    char temporary[PATH_MAX + 32];
    snprintf(record, sizeof record, "%s/rec", dir);
    (void)mkdir(record, 0755);
    snprintf(record, sizeof record, "%s/rec/%s.rec", dir, clean);
    FILE *out = smith_cov_open(record, temporary, sizeof temporary);
    if (out == nullptr || !smith_cov_write_all(dir)) return 1;
    fputs("smith-coverage 1\n", out);
    smith_cov_test_names[0] = name;
    for (size_t f = 0; f < smith_cov_function_count; f++) { // bounded by the functions
        if (smith_cov_function_run[f]) smith_cov_set(smith_cov_test_bits[0], f);
    }
    smith_cov_write_test(out, 0);
    smith_cov_write_lines(out, "cov", smith_cov_cover);
    return smith_cov_close(out, temporary, record) ? 0 : 1;
}

// The profiles of a test as llvm-cov's lcov, in smith's own process (shim/coverage.cpp): `binaries` are the programs
// that wrote them. The stage0 has no LLVM and makes no records.
#ifdef SMITH_IN_PROCESS
int smith_coverage_lcov(const char *list, const char *const *binaries, size_t binary_count, const char *root,
                        const char *profdata, const char *lcov, char *problem, size_t problem_size);
#else
static int smith_coverage_lcov(const char *list, const char *const *binaries, size_t binary_count, const char *root,
                               const char *profdata, const char *lcov, char *problem, size_t problem_size) {
    (void)list;
    (void)binaries;
    (void)binary_count;
    (void)root;
    (void)profdata;
    (void)lcov;
    snprintf(problem, problem_size, "this smith has no LLVM inside");
    return 1;
}
#endif

constexpr size_t smith_cov_binaries_max = 16;

// SMITH_COVERAGE_BINARY: the programs the test's processes were built from, separated by `:` (the runner first).
static size_t smith_cov_binaries(char *text, const char **out) {
    size_t count = 0;
    for (char *at = text; at != nullptr && *at != '\0' && count < smith_cov_binaries_max;) { // bounded by the programs
        out[count++] = at;
        at = strchr(at, ':');
        if (at != nullptr) *at++ = '\0';
    }
    return count;
}

// `smith test --coverage merge <test name>`: SMITH_COVERAGE_DIR (where the profiles are) and SMITH_COVERAGE_BINARY (the
// instrumented programs, separated by `:`) say where.
static double smith_cov_cpu_seconds; // the CPU time the test took, for its record (issue #506)

static int smith_cov_merge(const char *name, bool pass) {
    const char *dir = smith_cov_env("SMITH_COVERAGE_DIR");
    const char *binary = smith_cov_env("SMITH_COVERAGE_BINARY");
    if (dir == nullptr || binary == nullptr) {
        fputs("smith test --coverage merge: set SMITH_COVERAGE_DIR and SMITH_COVERAGE_BINARY\n", stderr);
        return 1;
    }
    const char *repo = smith_cov_env("SMITH_REPO");
    if (repo != nullptr)
        snprintf(smith_cov_root, sizeof smith_cov_root, "%s", repo);
    else if (getcwd(smith_cov_root, sizeof smith_cov_root) == nullptr)
        return 1;
    char clean[256];
    char raw[PATH_MAX];
    char list[PATH_MAX];
    char data[PATH_MAX];
    char lcov[PATH_MAX];
    char binaries_text[PATH_MAX];
    const char *binaries[smith_cov_binaries_max];
    char problem[512] = "";
    smith_cov_clean_name(name, clean, sizeof clean);
    snprintf(raw, sizeof raw, "%s/raw/%s", dir, clean);
    snprintf(list, sizeof list, "%s.list", raw);
    snprintf(data, sizeof data, "%s.profdata", raw);
    snprintf(lcov, sizeof lcov, "%s.lcov", raw);
    snprintf(binaries_text, sizeof binaries_text, "%s", binary);
    size_t binary_count = smith_cov_binaries(binaries_text, binaries);
    if (smith_cov_raw_list(raw, list, false) == 0) return 0; // the test ran no instrumented program
    int status = smith_coverage_lcov(list, binaries, binary_count, smith_cov_root, data, lcov, problem, sizeof problem);
    (void)smith_cov_raw_list(raw, list, true);
    (void)rmdir(raw);
    (void)unlink(list);
    (void)unlink(data);
    bool read = status == 0 && smith_cov_lcov(lcov);
    (void)unlink(lcov);
    if (!read) {
        if (problem[0] == '\0') snprintf(problem, sizeof problem, "no file below %s was counted", smith_cov_root);
        fprintf(stderr, "smith test --coverage merge: no record for %s (%s)\n", name, problem);
        return 1;
    }
    if (!smith_rec_write(dir, name, clean, pass))
        fprintf(stderr, "smith test --coverage merge: no record file for %s\n", name);
    return smith_cov_merge_record(dir, name, clean);
}

// `smith test --coverage join <dir> <commit> <table>`: all.tsv and every record of <dir>/rec into one table.
static int smith_cov_join(const char *dir, const char *commit, const char *table) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/all.tsv", dir);
    if (!smith_cov_load(path)) {
        fprintf(stderr, "smith test --coverage join: %s is missing or malformed\n", path);
        return 1;
    }
    snprintf(path, sizeof path, "%s/rec", dir);
    DIR *folder = opendir(path);
    struct dirent *entry = nullptr;
    for (size_t guard = 0; folder != nullptr && (entry = readdir(folder)) != nullptr && guard < (size_t)1 << 20;
         guard++) { // the records
        char record[PATH_MAX];
        snprintf(record, sizeof record, "%s/%s", path, entry->d_name);
        if (strstr(entry->d_name, ".rec") != nullptr) (void)smith_cov_load(record);
    }
    if (folder != nullptr) closedir(folder);
    char temporary[PATH_MAX + 32];
    FILE *out = smith_cov_open(table, temporary, sizeof temporary);
    if (out == nullptr) return 1;
    fprintf(out, "smith-coverage 1\ncommit\t%s\n", commit);
    smith_cov_write_functions(out);
    smith_cov_write_lines(out, "inst", smith_cov_inst);
    smith_cov_write_lines(out, "cov", smith_cov_cover);
    for (size_t t = 0; t < smith_cov_test_count; t++) // bounded by the tests
        smith_cov_write_test(out, t);
    fprintf(stderr, "smith test --coverage join: %zu tests, %zu functions\n", smith_cov_test_count,
            smith_cov_function_count);
    return smith_cov_close(out, temporary, table) ? 0 : 1;
}

// ---- The changed lines

constexpr size_t smith_cov_hunks_max = 8192;

typedef struct {
    int file; // an index of smith_cov_files, or -1 when the table does not know the file
    const char *path;
    uint32_t first; // the first line of the hunk on the side being read
    uint32_t count; // 0 for an insertion after `first`
    bool code;      // some line of it is neither blank nor a comment
} SmithCovHunk;

static SmithCovHunk smith_cov_hunks[smith_cov_hunks_max];
static size_t smith_cov_hunk_count;
static char smith_cov_diff_path[PATH_MAX];
static bool smith_cov_new_side;

static bool smith_cov_code_line(const char *text) {
    while (*text == ' ' || *text == '\t')
        text++;
    return *text != '\0' && strncmp(text, "//", 2) != 0 && strncmp(text, "/*", 2) != 0 && *text != '*' && *text != '\r';
}

// One line of `git diff -U0`: the hunk headers, and whether a changed line is code.
static void smith_cov_diff_line(char *line) {
    if (strncmp(line, "diff --git ", 11) == 0) {
        char *b = strstr(line, " b/");
        snprintf(smith_cov_diff_path, sizeof smith_cov_diff_path, "%s", b != nullptr ? b + 3 : "");
    } else if (strncmp(line, "@@ ", 3) == 0 && smith_cov_hunk_count < smith_cov_hunks_max) {
        const char *side = strchr(line, smith_cov_new_side ? '+' : '-');
        char *end = nullptr;
        unsigned long first = side != nullptr ? strtoul(side + 1, &end, 10) : 0;
        unsigned long count = side != nullptr && *end == ',' ? strtoul(end + 1, nullptr, 10) : 1;
        int file = smith_cov_file_find(smith_cov_diff_path);
        smith_cov_hunks[smith_cov_hunk_count++] =
            (SmithCovHunk){file, smith_cov_copy(smith_cov_diff_path), (uint32_t)first, (uint32_t)count, false};
    } else if (smith_cov_hunk_count > 0 && (line[0] == '+' || line[0] == '-') && strncmp(line, "+++", 3) != 0 &&
               strncmp(line, "---", 3) != 0) {
        if (smith_cov_code_line(line + 1)) smith_cov_hunks[smith_cov_hunk_count - 1].code = true;
    }
}

// The function with the least range around `line` of `file`, or -1.
static int smith_cov_function_at(int file, uint32_t line) {
    int best = -1;
    for (size_t f = 0; file >= 0 && f < smith_cov_function_count; f++) { // bounded by the functions
        const SmithCovFunction *fn = &smith_cov_functions[f];
        if ((int)fn->file != file || fn->first > line || fn->last < line) continue;
        if (best < 0 || fn->last - fn->first < smith_cov_functions[best].last - smith_cov_functions[best].first)
            best = (int)f;
    }
    return best;
}

// `smith test --coverage report <table> <base> <out>`: the table's totals, and the lines the diff from <base> to HEAD
// added that coverage counts and no test ran, each with the commit that wrote it. The file is documented in
// knowledge/coverage-selection.md.
static int smith_cov_report(const char *table, const char *base, const char *output) {
    if (!smith_cov_load(table)) {
        fprintf(stderr, "smith test --coverage report: %s is missing or malformed\n", table);
        return 1;
    }
    FILE *out = fopen(output, "w");
    if (out == nullptr) return 1;
    size_t lines = 0;
    size_t covered = 0;
    for (size_t i = 0; i < smith_cov_file_count; i++) {             // bounded by the files
        for (size_t line = 0; line < smith_cov_lines_max; line++) { // bounded by the lines
            lines += smith_cov_get(smith_cov_inst[i], line) ? 1 : 0;
            covered += smith_cov_get(smith_cov_inst[i], line) && smith_cov_get(smith_cov_cover[i], line) ? 1 : 0;
        }
    }
    size_t ran = 0;
    for (size_t f = 0; f < smith_cov_function_count; f++) { // bounded by the functions
        bool any = false;
        for (size_t t = 0; t < smith_cov_test_count && !any; t++)
            any = smith_cov_get(smith_cov_test_bits[t], f);
        ran += any ? 1 : 0;
    }
    fprintf(out, "coverage\t%s\tlines\t%zu\t%zu\tfunctions\t%zu\t%zu\n", smith_cov_commit, covered, lines, ran,
            smith_cov_function_count);
    char range[300];
    snprintf(range, sizeof range, "%s..HEAD", base);
    smith_cov_new_side = true;
    const char *const diff[] = {"diff", "-U0", "--no-renames", range, "--", nullptr};
    if (!smith_changed_git(diff, smith_cov_diff_line)) {
        fprintf(out, "changed\tunknown\tunknown\n");
        return fclose(out) == 0 ? 0 : 1;
    }
    size_t changed = 0;
    size_t uncovered = 0;
    for (size_t h = 0; h < smith_cov_hunk_count; h++) { // bounded by the hunks
        const SmithCovHunk *hunk = &smith_cov_hunks[h];
        for (uint32_t line = hunk->first; hunk->file >= 0 && line < hunk->first + hunk->count; line++) {
            if (!smith_cov_get(smith_cov_inst[hunk->file], line)) continue;
            changed++;
            if (smith_cov_get(smith_cov_cover[hunk->file], line)) continue;
            int fn = smith_cov_function_at(hunk->file, line);
            fprintf(out, "uncovered\t%s\t%u\t%s\n", hunk->path, line, fn >= 0 ? smith_cov_functions[fn].name : "-");
            uncovered++;
        }
    }
    fprintf(out, "changed\t%zu\t%zu\n", changed, uncovered);
    return fclose(out) == 0 ? 0 : 1;
}

// The entry of `smith test --coverage <merge|join|report> ...`.
static int smith_coverage_main(int argc, char **argv) {
    if (argc >= 2 && argc <= 4 && strcmp(argv[0], "merge") == 0) {
        smith_cov_cpu_seconds = argc == 4 ? strtod(argv[3], nullptr) : 0;
        return smith_cov_merge(argv[1], argc == 2 || strcmp(argv[2], "fail") != 0);
    }
    if (argc == 4 && strcmp(argv[0], "join") == 0) return smith_cov_join(argv[1], argv[2], argv[3]);
    if (argc == 4 && strcmp(argv[0], "report") == 0) return smith_cov_report(argv[1], argv[2], argv[3]);
    fputs("usage: smith test --coverage merge <test> [pass|fail [cpu seconds]] | join <dir> <commit> <table> | report "
          "<table> <base> "
          "<out>\n",
          stderr);
    return 2;
}
