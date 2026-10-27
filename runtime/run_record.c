// The run record (issue #535, knowledge/run-records.md): with SMITH_RECORD naming a file, a smith invoked from outside
// runs its work as a child and appends one JSON line to the file when it ends: the verb and arguments (paths relative
// to the project), the exit status, wall and CPU time, each diagnostic's id, place and first line, and the names of
// what failed. Unset, nothing here runs. The child has SMITH_RECORDING set, so a smith it starts (the build's compiler
// links) is part of this record and writes none of its own; its stderr comes through a pipe, is forwarded as it comes,
// and is read for the diagnostics; with a terminal on stderr nothing is piped and the line has no diagnostics.
// Local only: the file is whatever the variable names.

constexpr size_t smith_run_record_text_max = (size_t)1 << 20; // stderr kept for reading the diagnostics
constexpr size_t smith_run_record_line_max = (size_t)1 << 15;
constexpr size_t smith_run_record_diagnostics_max = 24;
constexpr size_t smith_run_record_failed_max = 24;
constexpr size_t smith_run_record_args_max = 40;

typedef struct {
    char *buffer;
    size_t used;
    size_t capacity;
} SmithRunRecordLine;

static void smith_run_record_raw(SmithRunRecordLine *line, const char *text, size_t length) {
    for (size_t i = 0; i < length && line->used + 1 < line->capacity; i++)
        line->buffer[line->used++] = text[i];
    line->buffer[line->used] = '\0';
}

// A JSON string, cut at `limit` bytes of source text.
static void smith_run_record_string(SmithRunRecordLine *line, const char *text, size_t limit) {
    smith_run_record_raw(line, "\"", 1);
    for (size_t i = 0; i < limit && text[i] != '\0'; i++) { // bounded by the limit
        unsigned char c = (unsigned char)text[i];
        char piece[8];
        if (c == '"' || c == '\\') {
            piece[0] = '\\';
            piece[1] = (char)c;
            smith_run_record_raw(line, piece, 2);
        } else if (c < 0x20) {
            int written = snprintf(piece, sizeof piece, "\\u%04x", c);
            smith_run_record_raw(line, piece, (size_t)written);
        } else {
            smith_run_record_raw(line, text + i, 1);
        }
    }
    smith_run_record_raw(line, "\"", 1);
}

static void smith_run_record_format(SmithRunRecordLine *line, const char *format, ...) {
    char piece[128];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(piece, sizeof piece, format, args);
    va_end(args);
    if (written > 0)
        smith_run_record_raw(line, piece, (size_t)written < sizeof piece ? (size_t)written : sizeof piece - 1);
}

// The id of a diagnostic: the tag in brackets at the end of its text ([-Wunused-variable], [smith-effects]), else the
// text with its quoted names and numbers replaced by `_`, so one message is one id wherever it appears.
static void smith_run_record_id(const char *text, char *id, size_t capacity) {
    size_t length = strlen(text);
    while (length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\r'))
        length--;
    if (length > 2 && text[length - 1] == ']') {
        size_t start = length - 1;
        while (start > 0 && text[start - 1] != '[')
            start--;
        if (start > 0 && length - start < capacity && length - start > 1) {
            snprintf(id, capacity, "%.*s", (int)(length - 1 - start), text + start);
            return;
        }
    }
    size_t used = 0;
    char quote = '\0';
    for (size_t i = 0; i < length && used + 2 < capacity && used < 80; i++) { // bounded by the text
        char c = text[i];
        if (quote != '\0') {
            if (c == quote) quote = '\0';
            continue;
        }
        if (c == '\'' || c == '`') {
            quote = c == '`' ? '\'' : c;
            id[used++] = '_';
        } else if (c >= '0' && c <= '9') {
            if (used == 0 || id[used - 1] != '_') id[used++] = '_';
        } else {
            id[used++] = c;
        }
    }
    id[used] = '\0';
}

// `path:line:col: error: text` (or warning): fills the parts, false for any other line.
static bool smith_run_record_parse(const char *line, char *place, size_t place_capacity, const char **text) {
    const char *marker = strstr(line, ": error: ");
    size_t skip = 9;
    if (marker == nullptr) {
        marker = strstr(line, ": warning: ");
        skip = 11;
    }
    if (marker == nullptr || marker == line) return false;
    // the place ends in :line:col
    const char *colon2 = marker;
    const char *colon1 = colon2;
    while (colon1 > line && colon1[-1] >= '0' && colon1[-1] <= '9')
        colon1--;
    if (colon1 == colon2 || colon1[-1] != ':') return false;
    snprintf(place, place_capacity, "%.*s", (int)(marker - line), line);
    *text = marker + skip;
    return true;
}

static bool smith_run_record_starts(const char *line, const char *prefix) {
    return strncmp(line, prefix, strlen(prefix)) == 0;
}

typedef struct {
    char id[96];
    char place[160];
    char text[200];
} SmithRunRecordDiagnostic;

static SmithRunRecordDiagnostic smith_run_record_diagnostics[smith_run_record_diagnostics_max];
static size_t smith_run_record_diagnostic_count;
static char smith_run_record_failed[smith_run_record_failed_max][160];
static size_t smith_run_record_failed_count;

static void smith_run_record_read_line(char *line) {
    char place[160];
    const char *text = nullptr;
    if (smith_run_record_parse(line, place, sizeof place, &text)) {
        if (smith_run_record_diagnostic_count < smith_run_record_diagnostics_max) {
            SmithRunRecordDiagnostic *item = &smith_run_record_diagnostics[smith_run_record_diagnostic_count++];
            smith_run_record_id(text, item->id, sizeof item->id);
            snprintf(item->place, sizeof item->place, "%s", place);
            snprintf(item->text, sizeof item->text, "%s", text);
        }
        return;
    }
    const char *name = nullptr;
    if (smith_run_record_starts(line, "FAIL ")) name = line + 5;
    // `smith check: <part> FAILED` and the runner's `FAIL <test> with seed N`
    char part[64];
    if (name == nullptr && smith_run_record_starts(line, "smith check: ")) {
        const char *end = strstr(line, " FAILED");
        if (end != nullptr && (size_t)(end - line) > 13 && (size_t)(end - line - 13) < sizeof part) {
            snprintf(part, sizeof part, "%.*s", (int)(end - line - 13), line + 13);
            name = part;
        }
    }
    if (name != nullptr && smith_run_record_failed_count < smith_run_record_failed_max) {
        char *target = smith_run_record_failed[smith_run_record_failed_count++];
        snprintf(target, sizeof smith_run_record_failed[0], "%s", name);
        char *with = strstr(target, " with seed");
        if (with != nullptr) *with = '\0';
    }
}

// The verbs that are run to completion; the long-lived ones (lsp, dap, mcp, dbg) and the helpers are not recorded.
static bool smith_run_record_wanted(const char *name, int argc, char **argv) {
    if (strcmp(name, "smith-cc") == 0 || strcmp(name, "smith-c++") == 0) return true;
    if (argc < 2) return false;
    static const char *const verbs[] = {"cc",  "c++",  "build", "check", "test", "prove",
                                        "fmt", "lint", "docs",  "run",   "new",  nullptr};
    for (size_t i = 0; verbs[i] != nullptr; i++) {
        if (strcmp(argv[1], verbs[i]) == 0) return true;
    }
    return false;
}

static void smith_run_record_arguments(SmithRunRecordLine *line, int argc, char **argv, const char *name) {
    char cwd[PATH_MAX];
    size_t cwd_length = getcwd(cwd, sizeof cwd) != nullptr ? strlen(cwd) : 0;
    smith_run_record_raw(line, "\"args\":[", 8);
    bool linked = strcmp(name, "smith-cc") == 0 || strcmp(name, "smith-c++") == 0;
    int first = linked ? 1 : 2;
    for (int i = first; i < argc && (size_t)(i - first) < smith_run_record_args_max; i++) {
        const char *argument = argv[i];
        if (cwd_length > 0 && strncmp(argument, cwd, cwd_length) == 0 && argument[cwd_length] == '/')
            argument += cwd_length + 1;
        if (i > first) smith_run_record_raw(line, ",", 1);
        smith_run_record_string(line, argument, 200);
    }
    smith_run_record_raw(line, "]", 1);
}

static void smith_run_record_write(const char *file, int argc, char **argv, const char *name, int status, double wall,
                                   double cpu) {
    static char buffer[smith_run_record_line_max];
    SmithRunRecordLine line = {.buffer = buffer, .capacity = sizeof buffer};
    time_t now = time(nullptr);
    struct tm parts;
    char stamp[32];
    gmtime_r(&now, &parts);
    strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &parts);
    char cwd[PATH_MAX];
    const char *folder = getcwd(cwd, sizeof cwd) != nullptr ? smith_base_name(cwd) : "";
    const char *lane = getenv("SMITH_LANE");
    const char *verb = strcmp(name, "smith-cc") == 0 || strcmp(name, "smith-c++") == 0 ? name : argv[1];
    smith_run_record_raw(&line, "{\"time\":\"", 9);
    smith_run_record_raw(&line, stamp, strlen(stamp));
    smith_run_record_raw(&line, "\",\"verb\":", 9);
    smith_run_record_string(&line, verb, 40);
    smith_run_record_raw(&line, ",\"project\":", 11);
    smith_run_record_string(&line, folder, 80);
    smith_run_record_raw(&line, ",\"lane\":", 8);
    smith_run_record_string(&line, lane != nullptr ? lane : "", 80);
    smith_run_record_raw(&line, ",", 1);
    smith_run_record_arguments(&line, argc, argv, name);
    smith_run_record_format(&line, ",\"exit\":%d,\"wall_ms\":%.0f,\"cpu_ms\":%.0f,\"diagnostics\":[", status,
                            wall * 1000, cpu * 1000);
    for (size_t i = 0; i < smith_run_record_diagnostic_count; i++) {
        const SmithRunRecordDiagnostic *item = &smith_run_record_diagnostics[i];
        if (i > 0) smith_run_record_raw(&line, ",", 1);
        smith_run_record_raw(&line, "{\"id\":", 6);
        smith_run_record_string(&line, item->id, sizeof item->id);
        smith_run_record_raw(&line, ",\"at\":", 6);
        smith_run_record_string(&line, item->place, sizeof item->place);
        smith_run_record_raw(&line, ",\"text\":", 8);
        smith_run_record_string(&line, item->text, sizeof item->text);
        smith_run_record_raw(&line, "}", 1);
    }
    smith_run_record_raw(&line, "],\"failed\":[", 12);
    for (size_t i = 0; i < smith_run_record_failed_count; i++) {
        if (i > 0) smith_run_record_raw(&line, ",", 1);
        smith_run_record_string(&line, smith_run_record_failed[i], sizeof smith_run_record_failed[0]);
    }
    smith_run_record_raw(&line, "]}\n", 3);
    int fd = open(file, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return;
    ssize_t written = write(fd, line.buffer, line.used);
    (void)written;
    close(fd);
}

static double smith_run_record_seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

// Runs this invocation as a child and records it; returns its exit status, or -1 when nothing is recorded (the
// caller then runs it as usual).
static int smith_run_record_run(int argc, char **argv, const char *self, const char *name) {
    const char *file = getenv("SMITH_RECORD");
    if (file == nullptr || file[0] != '/' || getenv("SMITH_RECORDING") != nullptr) return -1;
    if (!smith_run_record_wanted(name, argc, argv)) return -1;
    bool piped = isatty(2) == 0;
    int fds[2] = {-1, -1};
    if (piped && pipe(fds) != 0) return -1;
    double began = smith_run_record_seconds();
    pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        setenv("SMITH_RECORDING", "1", 1);
        if (piped) {
            close(fds[0]);
            dup2(fds[1], 2);
            close(fds[1]);
        }
        execv(self, argv);
        _exit(127);
    }
    if (piped) {
        close(fds[1]);
        static char text[smith_run_record_text_max];
        size_t used = 0;
        char buffer[4096];
        for (size_t reads = 0; reads < smith_run_record_text_max; reads++) { // until the child closes its stderr
            ssize_t count = read(fds[0], buffer, sizeof buffer);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            ssize_t sent = 0;
            while (sent < count) { // forwarded as it comes
                ssize_t part = write(2, buffer + sent, (size_t)(count - sent));
                if (part <= 0) break;
                sent += part;
            }
            if (used + (size_t)count < sizeof text) {
                memcpy(text + used, buffer, (size_t)count);
                used += (size_t)count;
            }
        }
        close(fds[0]);
        text[used] = '\0';
        char *at = text;
        for (size_t guard = 0; guard < used + 1 && *at != '\0'; guard++) { // one line a time
            char *end = strchr(at, '\n');
            if (end != nullptr) *end = '\0';
            smith_run_record_read_line(at);
            if (end == nullptr) break;
            at = end + 1;
        }
    }
    int status = 0;
    struct rusage usage;
    pid_t waited = -1;
    for (int i = 0; i < 1000 && waited < 0; i++) // bounded; only a signal interrupts the wait
        waited = wait4(child, &status, 0, &usage);
    double cpu = (double)usage.ru_utime.tv_sec + (double)usage.ru_stime.tv_sec +
                 ((double)usage.ru_utime.tv_usec + (double)usage.ru_stime.tv_usec) / 1e6;
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    smith_run_record_write(file, argc, argv, name, code, smith_run_record_seconds() - began, cpu);
    return code;
}
