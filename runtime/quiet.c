// Quiet output (issue #78): what a run prints is a line per stage, and the rest goes to a log under `.smith/log/`.
// A failure shows a bounded excerpt of its output (the lines from the first one that reports a failure) and the path
// of the log. `-v` (or SMITH_OUTPUT=full, which the outermost run sets for everything beneath it, so a test of smith
// that runs `smith test` sees what it always saw) prints everything as it happens. Included by smith.c.
constexpr size_t smith_quiet_lines = 30;
constexpr size_t smith_quiet_columns = 200;
constexpr size_t smith_quiet_excerpt_max = smith_quiet_lines * (smith_quiet_columns + 8) + 256;
constexpr size_t smith_quiet_text_max = (size_t)32 << 20;

// Whether this run prints everything: SMITH_OUTPUT=full is set by the run that captures its children's output.
static bool smith_quiet_full(void) {
    const char *mode = getenv("SMITH_OUTPUT");
    return mode != nullptr && strcmp(mode, "full") == 0;
}

// Whether a line reports a failure: what the executor, the test scripts, the runner and the compiler print.
static bool smith_quiet_failure_line(const char *line, size_t length) {
    if (strncmp(line, "ok ", 3) == 0 || strncmp(line, "PASS ", 5) == 0) return false; // a check that passed
    static const char *const marks[] = {"FAILED",    "FAIL ", "FAIL\t",  "error:",        "not ok",
                                        "Assertion", "ERROR", "cannot ", "failed (status"};
    for (size_t m = 0; m < sizeof marks / sizeof marks[0]; m++) {
        size_t mark = strlen(marks[m]);
        for (size_t at = 0; at + mark <= length; at++) { // bounded by the line
            if (memcmp(line + at, marks[m], mark) == 0) return true;
        }
    }
    return false;
}

static size_t smith_quiet_line_length(const char *text, size_t size, size_t at) {
    size_t length = 0;
    while (at + length < size && text[at + length] != '\n') // bounded by the text
        length++;
    return length;
}

static size_t smith_quiet_count_lines(const char *text, size_t size) {
    size_t lines = 0;
    for (size_t at = 0; at < size; lines++) // each step ends a line
        at += smith_quiet_line_length(text, size, at) + 1;
    return lines;
}

// Appends the line at `at` of the text to `out`, cut to smith_quiet_columns; returns the new length.
static size_t smith_quiet_put(char *out, size_t used, const char *text, size_t at, size_t length) {
    bool cut = length > smith_quiet_columns;
    size_t kept = cut ? smith_quiet_columns : length;
    memcpy(out + used, text + at, kept);
    used += kept;
    if (cut) {
        smith_copy_bytes(out + used, "...", 3);
        used += 3;
    }
    out[used++] = '\n';
    return used;
}

// The offset of line number `wanted` of the text.
static size_t smith_quiet_line_at(const char *text, size_t size, size_t wanted) {
    size_t at = 0;
    for (size_t line = 0; line < wanted && at < size; line++) // bounded by the lines
        at += smith_quiet_line_length(text, size, at) + 1;
    return at;
}

// The number of the first line that reports a failure at or after line `from`, or `total`.
static size_t smith_quiet_first_failure(const char *text, size_t size, size_t from, size_t total) {
    size_t at = smith_quiet_line_at(text, size, from);
    for (size_t line = from; line < total && at < size; line++) { // bounded by the lines
        size_t length = smith_quiet_line_length(text, size, at);
        if (smith_quiet_failure_line(text + at, length)) return line;
        at += length + 1;
    }
    return total;
}

// Writes into `out` (smith_quiet_excerpt_max bytes) at most smith_quiet_lines lines of `text`, from the first line that
// reports a failure or, when none does, the last ones, each cut to smith_quiet_columns. When that line is the
// executor's `FAILED:` header, its two lines come first and then the first failure the step itself printed (its
// own `FAIL`, an error). `hidden` is how many lines of the text are not in it. Returns the length.
static size_t smith_quiet_excerpt(char *out, const char *text, size_t size, size_t *hidden) {
    size_t total = smith_quiet_count_lines(text, size);
    size_t start = smith_quiet_first_failure(text, size, 0, total);
    size_t used = 0;
    size_t shown = 0;
    if (start == total) start = total > smith_quiet_lines ? total - smith_quiet_lines : 0;
    size_t at = smith_quiet_line_at(text, size, start);
    if (strncmp(text + at, "FAILED:", 7) == 0 && start + 2 < total) {
        size_t inner = smith_quiet_first_failure(text, size, start + 2, total);
        if (inner < total) { // the header and the command, then what the step reported
            for (; shown < 2; shown++) {
                size_t length = smith_quiet_line_length(text, size, at);
                used = smith_quiet_put(out, used, text, at, length);
                at += length + 1;
            }
            at = smith_quiet_line_at(text, size, inner);
            memcpy(out + used, "...\n", 4);
            used += 4;
        }
    }
    while (at < size && shown < smith_quiet_lines) { // bounded by the lines
        size_t length = smith_quiet_line_length(text, size, at);
        used = smith_quiet_put(out, used, text, at, length);
        at += length + 1;
        shown++;
    }
    out[used] = '\0';
    *hidden = total > shown ? total - shown : 0;
    return used;
}

// The path of the log of `name` (`.smith/log/<name>.log`), with the folders made. Characters that are not a name's are
// written as `_`.
static const char *smith_quiet_log_path(char *path, size_t capacity, const char *name) {
    (void)mkdir(".smith", 0755);
    (void)mkdir(".smith/log", 0755);
    int length = snprintf(path, capacity, ".smith/log/%s", name);
    for (size_t i = sizeof ".smith/log/" - 1; i < capacity && path[i] != '\0'; i++) {
        bool plain = isalnum((unsigned char)path[i]) || path[i] == '.' || path[i] == '-' || path[i] == '_';
        if (!plain) path[i] = '_';
    }
    if (length > 0 && (size_t)length + sizeof ".log" < capacity) memcpy(path + length, ".log", sizeof ".log");
    return path;
}

// Reads a log whole (up to smith_quiet_text_max bytes; the rest is dropped) into a static buffer.
static const char *smith_quiet_read(const char *path, size_t *size) {
    static char text[smith_quiet_text_max];
    FILE *file = fopen(path, "r");
    *size = file != nullptr ? fread(text, 1, sizeof text - 1, file) : 0;
    if (file != nullptr) fclose(file);
    text[*size] = '\0';
    return text;
}

// Reads a number at `*at` that is followed by `after`; moves `*at` past both.
static bool smith_quiet_field(const char **at, size_t *value, const char *after) {
    if (!isdigit((unsigned char)**at)) return false;
    char *end = nullptr;
    *value = strtoull(*at, &end, 10);
    size_t length = strlen(after);
    if (strncmp(end, after, length) != 0) return false;
    *at = end + length;
    return true;
}

// What a stage says about itself: `smith test` ends with "smith test: N tasks, ...", and `smith prove` prints "N
// proved, M failed, K trusted ..." for each group of sources, which add up. Written to `out` (empty when the log says
// nothing).
// A prove run also says, for each group, "N of M functions have contracts" (issue #51); the sums go to `coverage` as
// "N of M functions" (empty when the log has no such line).
static void smith_quiet_detail(const char *verb, const char *text, size_t size, char *out, size_t capacity,
                               char *coverage, size_t coverage_capacity) {
    out[0] = '\0';
    coverage[0] = '\0';
    bool prove = strcmp(verb, "prove") == 0;
    size_t covered[2] = {};
    bool covers = false;
    size_t sum[3] = {};
    size_t tasks = 0;
    bool seen = false;
    bool lint = strcmp(verb, "lint") == 0;
    size_t linted = 0;
    for (size_t at = 0; at < size; at += smith_quiet_line_length(text, size, at) + 1) { // bounded by the text
        const char *line = text + at;
        size_t one = 0, two = 0, three = 0;
        if (prove && smith_quiet_field(&line, &one, " of ") && smith_quiet_field(&line, &two, " functions have")) {
            covered[0] += one;
            covered[1] += two;
            covers = true;
        } else if (prove && smith_quiet_field(&line, &one, " proved, ") &&
                   smith_quiet_field(&line, &two, " failed, ") && smith_quiet_field(&line, &three, " trusted")) {
            sum[0] += one;
            sum[1] += two;
            sum[2] += three;
            seen = true;
        } else if (lint) { // "smith lint: linted N files"
            if (strncmp(line, "smith lint: linted ", 19) == 0) {
                line += 19;
                if (smith_quiet_field(&line, &linted, " files")) seen = true;
            }
        } else if (!prove && strncmp(line, "smith test: ", 12) == 0) {
            line += 12;
            if (smith_quiet_field(&line, &tasks, " tasks")) seen = true;
        } else if (!prove) { // the runner's "    846 passed, 0 failed in 5.53 s"
            while (*line == ' ')
                line++;
            if (smith_quiet_field(&line, &one, " passed, ") && smith_quiet_field(&line, &two, " failed in ")) {
                sum[0] += one;
                sum[1] += two;
                seen = true;
            }
        }
    }
    if (covers) snprintf(coverage, coverage_capacity, "%zu of %zu functions", covered[0], covered[1]);
    if (!seen) return;
    if (lint) {
        snprintf(out, capacity, "%zu files", linted);
    } else if (prove) {
        snprintf(out, capacity, "%zu proved, %zu failed, %zu trusted", sum[0], sum[1], sum[2]);
    } else if (tasks > 0) {
        snprintf(out, capacity, "%zu tasks, %zu passed", tasks, sum[0]);
    } else {
        snprintf(out, capacity, "%zu passed", sum[0]);
    }
}
