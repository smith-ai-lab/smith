// smith's test library: the checks' failures, the memory of a test, and the runner, which is this library's main.
// The design is in include/smith/test.h and docs/build-c.md.
//
// The runner forks a child for each test. The child writes what a failure needs to know (the check's file and line, the
// values, the note) to a block of memory it shares with the runner, so it survives however the child ends: a failed
// check, a crash or a kill. Everything the child prints goes down a pipe, and is shown only when the test fails.
#include <smith/test.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef __has_feature
#define __has_feature(feature) 0
#endif

constexpr size_t smith_test_note_capacity = 160;
constexpr size_t smith_test_detail_capacity = 1024;
constexpr size_t smith_test_text_capacity = 128;          // one formatted value
constexpr size_t smith_test_memory_capacity = 256U << 20; // every test arena of one test, or of one key, together
constexpr size_t smith_test_output_capacity = 64U << 10;  // what a failing test shows of its output
constexpr size_t smith_test_output_limit = 16U << 20;     // a child that writes more is killed
constexpr size_t smith_test_jobs_max = 256;
constexpr size_t smith_test_alternate_stack = 64U << 10;
constexpr int smith_test_failure_status = 99; // a child whose check failed exits with this
constexpr int smith_test_signal_status = 128; // a child that catches a fatal signal exits with 128 + its number
constexpr int smith_test_usage_status = 2;

// What a child tells the runner, in memory they share. A block is reset before each test and each key.
typedef struct {
    const char *file; // the failed check's file: a string literal of the program, valid in every process forked from it
    int line;         // 0 when no check has failed
    uint64_t seed;    // the property test's current key
    char note[smith_test_note_capacity];
    char detail[smith_test_detail_capacity]; // the failed check's values, as lines
} SmithTestShared;

static SmithTestShared smith_test_fallback;
static SmithTestShared *smith_test_shared = &smith_test_fallback;
static SmithTestShared *smith_test_shared_slots; // the runner's: one per job, and one more for shrinking
static unsigned char *smith_test_memory;
static size_t smith_test_memory_used; // a multiple of smith_arena_align_max
static unsigned smith_test_timeout_seconds = 10;

static void smith_test_reset(void) {
    smith_test_memory_used = 0;
    smith_test_shared->file = nullptr;
    smith_test_shared->line = 0;
    smith_test_shared->note[0] = '\0';
    smith_test_shared->detail[0] = '\0';
}

SmithArena smith_test_arena(size_t capacity) {
    assert(capacity > 0);
    size_t rounded = (capacity + smith_arena_align_max - 1) & ~(smith_arena_align_max - 1);
    if (smith_test_memory == nullptr || capacity > smith_test_memory_capacity ||
        rounded > smith_test_memory_capacity - smith_test_memory_used) {
        fprintf(stderr,
                "smith: the tests' memory is out of capacity: smith_test_arena(%zu) with %zu of %zu bytes used\n",
                capacity, smith_test_memory_used, smith_test_memory_capacity);
        fflush(stderr);
        abort();
    }
    SmithArena arena = smith_arena_make(smith_test_memory + smith_test_memory_used, capacity);
    smith_test_memory_used += rounded;
    return arena;
}

void smith_test_note(const char *format, ...) {
    assert(format != nullptr);
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(smith_test_shared->note, smith_test_note_capacity, format, arguments);
    va_end(arguments);
}

// Values.

// NOLINTBEGIN(bugprone-macro-parentheses): a field name and a type name cannot be parenthesized
#define SMITH_TEST_DEFINE_VALUE(suffix, Type, kind_name, type_name, field) \
    SmithTestValue smith_test_value_##suffix(Type value) { \
        return (SmithTestValue){.kind = kind_name, .type = type_name, .field = value}; \
    }
// NOLINTEND(bugprone-macro-parentheses)

SMITH_TEST_DEFINE_VALUE(bool, bool, SmithTestKind_Bool, "bool", bool_value)
SMITH_TEST_DEFINE_VALUE(char, char, SmithTestKind_Char, "char", char_value)
SMITH_TEST_DEFINE_VALUE(signed_char, signed char, SmithTestKind_Signed, "signed char", signed_value)
SMITH_TEST_DEFINE_VALUE(unsigned_char, unsigned char, SmithTestKind_Unsigned, "unsigned char", unsigned_value)
SMITH_TEST_DEFINE_VALUE(short, short, SmithTestKind_Signed, "short", signed_value)
SMITH_TEST_DEFINE_VALUE(unsigned_short, unsigned short, SmithTestKind_Unsigned, "unsigned short", unsigned_value)
SMITH_TEST_DEFINE_VALUE(int, int, SmithTestKind_Signed, "int", signed_value)
SMITH_TEST_DEFINE_VALUE(unsigned_int, unsigned int, SmithTestKind_Unsigned, "unsigned int", unsigned_value)
SMITH_TEST_DEFINE_VALUE(long, long, SmithTestKind_Signed, "long", signed_value)
SMITH_TEST_DEFINE_VALUE(unsigned_long, unsigned long, SmithTestKind_Unsigned, "unsigned long", unsigned_value)
SMITH_TEST_DEFINE_VALUE(long_long, long long, SmithTestKind_Signed, "long long", signed_value)
SMITH_TEST_DEFINE_VALUE(unsigned_long_long, unsigned long long, SmithTestKind_Unsigned, "unsigned long long",
                        unsigned_value)
SMITH_TEST_DEFINE_VALUE(float, float, SmithTestKind_Float, "float", float_value)
SMITH_TEST_DEFINE_VALUE(double, double, SmithTestKind_Double, "double", double_value)

SmithTestValue smith_test_value_pointer(const volatile void *value) {
    return (SmithTestValue){.kind = SmithTestKind_Pointer, .type = "pointer", .pointer_value = (uintptr_t)value};
}

constexpr int smith_test_less = -1;
constexpr int smith_test_equal = 0;
constexpr int smith_test_greater = 1;
constexpr int smith_test_unordered = 2; // a NaN

static int smith_test_order_signed(long long a, long long b) {
    return a < b ? smith_test_less : a > b ? smith_test_greater : smith_test_equal;
}

static int smith_test_order_unsigned(unsigned long long a, unsigned long long b) {
    return a < b ? smith_test_less : a > b ? smith_test_greater : smith_test_equal;
}

static int smith_test_order_real(double a, double b) {
    if (a < b) return smith_test_less;
    if (a > b) return smith_test_greater;
    return a == b ? smith_test_equal : smith_test_unordered; // a NaN is unordered
}

static int smith_test_order(SmithTestValue a, SmithTestValue b) {
    assert(a.kind == b.kind);
    switch (a.kind) {
    case SmithTestKind_Unsigned:
        return smith_test_order_unsigned(a.unsigned_value, b.unsigned_value);
    case SmithTestKind_Signed:
        return smith_test_order_signed(a.signed_value, b.signed_value);
    case SmithTestKind_Float:
        return smith_test_order_real((double)a.float_value, (double)b.float_value);
    case SmithTestKind_Double:
        return smith_test_order_real(a.double_value, b.double_value);
    case SmithTestKind_Bool:
        return smith_test_order_unsigned(a.bool_value ? 1 : 0, b.bool_value ? 1 : 0);
    case SmithTestKind_Char:
        return smith_test_order_signed(a.char_value, b.char_value);
    case SmithTestKind_Pointer:
        return smith_test_order_unsigned(a.pointer_value, b.pointer_value);
    }
    return smith_test_unordered;
}

bool smith_test_compare(SmithTestValue a, const char *op, SmithTestValue b) {
    assert(op != nullptr);
    int order = smith_test_order(a, b);
    if (strcmp(op, "==") == 0) return order == smith_test_equal;
    if (strcmp(op, "!=") == 0) return order != smith_test_equal;
    if (strcmp(op, "<") == 0) return order == smith_test_less;
    if (strcmp(op, "<=") == 0) return order == smith_test_less || order == smith_test_equal;
    if (strcmp(op, ">") == 0) return order == smith_test_greater;
    if (strcmp(op, ">=") == 0) return order == smith_test_greater || order == smith_test_equal;
    fprintf(stderr, "smith: SMITH_CHECK: the operator must be ==, !=, <, <=, > or >=, not '%s'\n", op);
    fflush(stderr);
    abort();
}

bool smith_test_within(double distance, double tolerance) {
    return distance <= tolerance; // false when either is a NaN
}

float smith_test_distance_float(float a, float b) {
    if (a == b) return 0.0F; // equal infinities are no distance apart
    return a > b ? a - b : b - a;
}

double smith_test_distance_double(double a, double b) {
    if (a == b) return 0.0;
    return a > b ? a - b : b - a;
}

// The shortest text that reads back as the same real, then its exact value in hex, so a difference in the last bit
// shows: "0.30000000000000004 [0x1.3333333333334p-2]".
static void smith_test_format_real(double value, bool single, char *text, size_t capacity) {
    if (!isfinite(value)) {
        snprintf(text, capacity, "%g", value);
        return;
    }
    for (int digits = 1; digits <= 17; digits++) { // 17 digits tell any two doubles apart
        snprintf(text, capacity, "%.*g", digits, value);
        bool same = single ? strtof(text, nullptr) == (float)value : strtod(text, nullptr) == value;
        if (same) break;
    }
    size_t length = strlen(text);
    snprintf(text + length, capacity - length, " [%a]", value);
}

// Writes the value the way C would spell it.
static void smith_test_format(SmithTestValue value, char *text, size_t capacity) {
    assert(text != nullptr && capacity == smith_test_text_capacity);
    switch (value.kind) {
    case SmithTestKind_Unsigned:
        snprintf(text, capacity, "%llu", value.unsigned_value);
        return;
    case SmithTestKind_Signed:
        snprintf(text, capacity, "%lld", value.signed_value);
        return;
    case SmithTestKind_Float:
        smith_test_format_real((double)value.float_value, true, text, capacity);
        return;
    case SmithTestKind_Double:
        smith_test_format_real(value.double_value, false, text, capacity);
        return;
    case SmithTestKind_Bool:
        snprintf(text, capacity, "%s", value.bool_value ? "true" : "false");
        return;
    case SmithTestKind_Char:
        if (value.char_value >= ' ' && value.char_value <= '~')
            snprintf(text, capacity, "'%c'", value.char_value);
        else
            snprintf(text, capacity, "%d", value.char_value);
        return;
    case SmithTestKind_Pointer:
        if (value.pointer_value == 0)
            snprintf(text, capacity, "nullptr");
        else
            snprintf(text, capacity, "0x%lx", (unsigned long)value.pointer_value);
        return;
    }
}

typedef struct {
    const char *text; // the expression as written
    SmithTestValue value;
    char formatted[smith_test_text_capacity];
    bool literal; // the expression is the value itself, like 500
} SmithTestLine;

// Appends to the shared detail, silently dropping what does not fit.
[[gnu::format(printf, 2, 3)]] static void smith_test_detail_add(size_t *used, const char *format, ...) {
    if (*used >= smith_test_detail_capacity - 1) return;
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(smith_test_shared->detail + *used, smith_test_detail_capacity - *used, format, arguments);
    va_end(arguments);
    if (written > 0) *used += (size_t)written;
    if (*used > smith_test_detail_capacity - 1) *used = smith_test_detail_capacity - 1;
}

// One line per value, "expression = value (type)", with the three columns aligned. A literal has no "= value".
static void smith_test_write_values(SmithTestLine *lines, size_t count) {
    assert(count > 0 && count <= 4);
    size_t text_width = 0;
    size_t value_width = 0;
    for (size_t i = 0; i < count; i++) {
        smith_test_format(lines[i].value, lines[i].formatted, smith_test_text_capacity);
        lines[i].literal = strcmp(lines[i].text, lines[i].formatted) == 0;
        size_t formatted_length = strlen(lines[i].formatted);
        if (formatted_length > value_width) value_width = formatted_length;
        if (!lines[i].literal && strlen(lines[i].text) > text_width) text_width = strlen(lines[i].text);
    }
    size_t used = 0;
    smith_test_shared->detail[0] = '\0';
    for (size_t i = 0; i < count; i++) {
        if (lines[i].literal) {
            smith_test_detail_add(&used, "%-*s   (%s)\n",
                                  (int)(text_width > 0 ? text_width + 3 + value_width : value_width), lines[i].text,
                                  lines[i].value.type);
        } else {
            smith_test_detail_add(&used, "%-*s = %-*s   (%s)\n", (int)text_width, lines[i].text, (int)value_width,
                                  lines[i].formatted, lines[i].value.type);
        }
    }
}

[[noreturn]] static void smith_test_end(const char *file, int line) {
    fflush(nullptr);
    smith_test_shared->file = file;
    smith_test_shared->line = line;
    _exit(smith_test_failure_status);
}

void smith_test_fail_condition(const char *file, int line) {
    assert(file != nullptr && line > 0);
    snprintf(smith_test_shared->detail, smith_test_detail_capacity, "the condition is false\n");
    smith_test_end(file, line);
}

void smith_test_fail_compare(const char *a_text, const char *b_text, SmithTestValue a, SmithTestValue b,
                             const char *file, int line) {
    assert(a_text != nullptr && b_text != nullptr && file != nullptr && line > 0);
    SmithTestLine lines[2] = {{.text = a_text, .value = a}, {.text = b_text, .value = b}};
    smith_test_write_values(lines, 2);
    smith_test_end(file, line);
}

void smith_test_fail_near(const char *a_text, const char *b_text, const char *tolerance_text,
                          const SmithTestValue values[4], const char *file, int line) {
    assert(a_text != nullptr && b_text != nullptr && tolerance_text != nullptr && values != nullptr);
    assert(file != nullptr && line > 0);
    SmithTestLine lines[4] = {{.text = a_text, .value = values[0]},
                              {.text = b_text, .value = values[1]},
                              {.text = tolerance_text, .value = values[2]},
                              {.text = "distance", .value = values[3]}};
    smith_test_write_values(lines, 4);
    smith_test_end(file, line);
}

// SMITH_CHECK_ABORTS.

typedef struct {
    pid_t child;
    int input; // the read end of the child's output
} SmithTestDeath;

static SmithTestDeath smith_test_death;

bool smith_test_abort_child(void) {
    int ends[2];
    if (pipe(ends) != 0) {
        perror("smith: pipe");
        abort();
    }
    fflush(nullptr); // or the child prints what is still buffered a second time
    pid_t child = fork();
    if (child < 0) {
        perror("smith: fork");
        abort();
    }
    if (child == 0) {
        close(ends[0]);
        dup2(ends[1], STDOUT_FILENO);
        dup2(ends[1], STDERR_FILENO);
        close(ends[1]);
        alarm(smith_test_timeout_seconds);
        return true;
    }
    close(ends[1]);
    smith_test_death = (SmithTestDeath){.child = child, .input = ends[0]};
    return false;
}

void smith_test_abort_returned(void) {
    fflush(nullptr);
    _exit(0);
}

// Reads the child's output to its end, keeping the start of it, and kills a child that floods it.
static size_t smith_test_death_read(char *text, size_t capacity) {
    size_t kept = 0;
    size_t total = 0;
    char chunk[4096];
    for (size_t reads = 0; reads <= smith_test_output_limit / sizeof chunk + 1; reads++) {
        ssize_t got = read(smith_test_death.input, chunk, sizeof chunk);
        if (got <= 0) break;
        size_t keep = (size_t)got < capacity - kept ? (size_t)got : capacity - kept;
        memcpy(text + kept, chunk, keep);
        kept += keep;
        total += (size_t)got;
        if (total >= smith_test_output_limit) {
            kill(smith_test_death.child, SIGKILL);
            break;
        }
    }
    close(smith_test_death.input);
    return kept;
}

// Why the child's end is not a death, or nullptr when it died: by a signal, by a caught signal's status, or by any
// other non-zero exit, which is how a sanitizer ends a process.
static const char *smith_test_death_verdict(int status) {
    int signal_number = 0;
    if (WIFSIGNALED(status)) signal_number = WTERMSIG(status);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 0;
    if (code > smith_test_signal_status) signal_number = code - smith_test_signal_status;
    if (signal_number == SIGALRM) return "expected an abort; it timed out";
    if (signal_number != 0) return nullptr;
    if (code == 0) return "expected an abort; it returned";
    if (code == smith_test_failure_status) return "expected an abort; a check inside it failed";
    return nullptr;
}

void smith_test_abort_judge(const char *file, int line) {
    assert(file != nullptr && line > 0);
    static char text[smith_test_output_capacity];
    size_t kept = smith_test_death_read(text, sizeof text);
    int status = 0;
    if (waitpid(smith_test_death.child, &status, 0) != smith_test_death.child) {
        perror("smith: waitpid");
        abort();
    }
    const char *verdict = smith_test_death_verdict(status);
    if (verdict == nullptr) return;
    if (kept > 0) fwrite(text, 1, kept, stderr); // the child's output is shown for a failure only
    snprintf(smith_test_shared->detail, smith_test_detail_capacity, "%s\n", verdict);
    smith_test_end(file, line);
}

// The runner.

typedef struct {
    const char *filter; // nullptr runs every test
    bool exact;         // a test has exactly the filter's name, so only that test runs
    bool seed_given;
    uint64_t seed;
    uint64_t seed_count;
    size_t jobs;
    unsigned timeout;
    bool shrink;
    bool list;
    bool verbose;
    bool color;
} SmithTestOptions;

constexpr uint64_t smith_test_seeds_default = 100;
constexpr uint64_t smith_test_seeds_max = 1'000'000'000;
constexpr uint64_t smith_test_seed_root = 0x5eed; // the key the properties' seeds are drawn from

typedef enum : unsigned char {
    SmithTestEnd_Passed,
    SmithTestEnd_Check,   // a check failed
    SmithTestEnd_Signal,  // killed by a signal, or caught one and exited with 128 plus its number
    SmithTestEnd_Timeout, // SIGALRM
    SmithTestEnd_Exit,    // exited with some other status
    SmithTestEnd_Flood,   // wrote more than smith_test_output_limit
} SmithTestEnd;

typedef struct {
    SmithTestEnd end;
    int number; // the signal, or the exit status
} SmithTestOutcome;

typedef struct {
    char text[smith_test_output_capacity];
    size_t kept;  // bytes in text
    size_t total; // bytes the child wrote
} SmithTestOutput;

// One test's run: filled in as its child runs and ends, and printed once every test before it has been.
typedef struct {
    pid_t child;
    int input; // the read end of the child's output, -1 once it has closed
    double start;
    double seconds;
    bool finished;
    SmithTestOutcome outcome;
    uint64_t seed; // a property test's key when the child ended
    const char *file;
    int line;
    char note[smith_test_note_capacity];
    char detail[smith_test_detail_capacity];
    SmithTestOutput output;
    unsigned shrink_runs; // the runs shrinking took, 0 when it did not run
    bool shrink_changed;  // it found a smaller failure
} SmithTestRun;

static void smith_test_die(const char *call) {
    perror(call);
    exit(smith_test_usage_status);
}

static double smith_test_seconds(void) {
    struct timespec now = {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

// Memory the runner and its children take from the system at startup: the runner has no allocator to ask.
static void *smith_test_map(size_t bytes, int flags) {
    // NOLINTNEXTLINE(bugprone-signed-bitwise): mmap's MAP_* flags are signed ints defined by the system header
    void *memory = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, flags | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED) smith_test_die("mmap");
    return memory;
}

static void smith_test_on_signal(int signal_number) {
    _exit(smith_test_signal_status + signal_number);
}

// A child that catches a fatal signal exits with 128 plus its number, so macOS writes no crash report for a death test
// or a crashing test. AddressSanitizer keeps its own SIGSEGV, SIGBUS and SIGFPE handlers, whose report ends in abort().
static void smith_test_catch_signals(void) {
    static unsigned char stack[smith_test_alternate_stack];
    stack_t alternate = {.ss_sp = stack, .ss_size = sizeof stack};
    sigaltstack(&alternate, nullptr);
    struct sigaction action = {.sa_handler = smith_test_on_signal, .sa_flags = SA_ONSTACK};
    sigemptyset(&action.sa_mask);
    const int signals[] = {SIGTRAP, SIGABRT,
                           SIGILL,
#if !__has_feature(address_sanitizer)
                           SIGSEGV, SIGBUS,
                           SIGFPE
#endif
    };
    for (size_t i = 0; i < sizeof signals / sizeof signals[0]; i++)
        sigaction(signals[i], &action, nullptr);
}

// Runs a property test on one key: it draws its inputs from the key, so the seed replays it.
static void smith_test_run_key(const SmithTest *test, const SmithTestOptions *options, uint64_t seed) {
    assert(test->property != nullptr);
    smith_test_shared->seed = seed;
    smith_test_reset();
    alarm(options->timeout);
    test->property(smith_key_make(seed));
}

static uint64_t smith_test_hook(SmithKey key, uint64_t n, uint64_t bits);
static bool smith_test_hooking; // the next child installs the hook

// In the child: send all output down the pipe, run the test and exit 0 if it returns.
[[noreturn]] static void smith_test_child(const SmithTest *test, const SmithTestOptions *options,
                                          SmithTestShared *shared, int output) {
    smith_test_shared = shared;
    smith_test_timeout_seconds = options->timeout;
    smith_test_catch_signals();
    if (smith_test_hooking) smith_random_hook = smith_test_hook;
    if (dup2(output, STDOUT_FILENO) < 0 || dup2(output, STDERR_FILENO) < 0) _exit(smith_test_usage_status);
    close(output);
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (test->plain != nullptr) {
        smith_test_reset();
        alarm(options->timeout);
        test->plain();
    } else if (options->seed_given) {
        smith_test_run_key(test, options, options->seed);
    } else {
        SmithKey root = smith_key_make(smith_test_seed_root);
        for (uint64_t i = 0; i < options->seed_count; i++)
            smith_test_run_key(test, options, smith_key_bits(root, i));
    }
    fflush(nullptr);
    _exit(0);
}

static const char *smith_test_signal_name(int signal_number) {
    switch (signal_number) {
    case SIGTRAP:
        return "SIGTRAP";
    case SIGABRT:
        return "SIGABRT";
    case SIGSEGV:
        return "SIGSEGV";
    case SIGBUS:
        return "SIGBUS";
    case SIGILL:
        return "SIGILL";
    case SIGFPE:
        return "SIGFPE";
    case SIGKILL:
        return "SIGKILL";
    default:
        return nullptr;
    }
}

static SmithTestOutcome smith_test_judge(int status, bool flood) {
    if (flood) return (SmithTestOutcome){.end = SmithTestEnd_Flood};
    int signal_number = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 0;
    if (code > smith_test_signal_status) signal_number = code - smith_test_signal_status;
    if (signal_number == SIGALRM) return (SmithTestOutcome){.end = SmithTestEnd_Timeout, .number = SIGALRM};
    if (signal_number != 0) return (SmithTestOutcome){.end = SmithTestEnd_Signal, .number = signal_number};
    if (code == 0) return (SmithTestOutcome){.end = SmithTestEnd_Passed};
    if (code == smith_test_failure_status) return (SmithTestOutcome){.end = SmithTestEnd_Check};
    return (SmithTestOutcome){.end = SmithTestEnd_Exit, .number = code};
}

// Forks the test's child, its output going down a pipe the runner reads.
static void smith_test_start(const SmithTest *test, const SmithTestOptions *options, SmithTestShared *shared,
                             SmithTestRun *run) {
    int ends[2];
    if (pipe(ends) != 0) smith_test_die("pipe");
    memset(shared, 0, sizeof *shared);
    fflush(nullptr);
    pid_t child = fork();
    if (child < 0) smith_test_die("fork");
    if (child == 0) {
        close(ends[0]);
        smith_test_child(test, options, shared, ends[1]);
    }
    close(ends[1]);
    run->child = child;
    run->input = ends[0];
    run->start = smith_test_seconds();
}

// Reads what is waiting from the child, keeping the start of it. Returns false once the child has closed its output or
// has written more than smith_test_output_limit, which kills it.
static bool smith_test_read(SmithTestRun *run) {
    SmithTestOutput *output = &run->output;
    char chunk[4096];
    ssize_t got = read(run->input, chunk, sizeof chunk);
    if (got <= 0) return false;
    size_t room = smith_test_output_capacity - output->kept;
    size_t keep = (size_t)got < room ? (size_t)got : room;
    memcpy(output->text + output->kept, chunk, keep);
    output->kept += keep;
    output->total += (size_t)got;
    if (output->total < smith_test_output_limit) return true;
    kill(run->child, SIGKILL);
    return false;
}

// Waits for the ended child and judges how it ended; what it left in the shared block is copied to the run.
static void smith_test_finish(SmithTestRun *run, const SmithTestShared *shared) {
    close(run->input);
    run->input = -1;
    int status = 0;
    if (waitpid(run->child, &status, 0) != run->child) smith_test_die("waitpid");
    run->seconds = smith_test_seconds() - run->start;
    run->outcome = smith_test_judge(status, run->output.total >= smith_test_output_limit);
    run->seed = shared->seed;
    run->file = shared->file;
    run->line = shared->line;
    memcpy(run->note, shared->note, sizeof run->note);
    memcpy(run->detail, shared->detail, sizeof run->detail);
    run->finished = true;
}

// Printing.

static const char *smith_test_paint(const SmithTestOptions *options, const char *code) {
    return options->color ? code : "";
}

static void smith_test_print_indented(const char *text, size_t length, const char *indent) {
    bool line_start = true;
    for (size_t i = 0; i < length; i++) {
        if (line_start && text[i] != '\n') fputs(indent, stdout);
        putchar(text[i]);
        line_start = text[i] == '\n';
    }
    if (!line_start) putchar('\n');
}

// Line `line` of the file, without its leading blanks and its newline; false when the file cannot be read.
static bool smith_test_source_line(const char *file, int line, char *text, size_t capacity) {
    FILE *source = file != nullptr ? fopen(file, "r") : nullptr;
    if (source == nullptr) return false;
    int current = 1;
    size_t used = 0;
    bool skipping = true; // leading blanks
    for (size_t reads = 0; reads < smith_test_output_limit; reads++) {
        int c = getc(source);
        if (c == EOF || (c == '\n' && current == line)) break;
        if (c == '\n') {
            current += 1;
            continue;
        }
        if (current != line || (skipping && (c == ' ' || c == '\t'))) continue;
        skipping = false;
        if (used + 1 < capacity) text[used++] = (char)c;
    }
    fclose(source);
    text[used] = '\0';
    return current == line && used > 0;
}

static void smith_test_print_reason(const SmithTestRun *run, const SmithTestOptions *options) {
    unsigned timeout = options->timeout;
    const char *name = smith_test_signal_name(run->outcome.number);
    switch (run->outcome.end) {
    case SmithTestEnd_Timeout:
        printf(": timed out after %u s", timeout);
        break;
    case SmithTestEnd_Signal:
        if (name != nullptr)
            printf(": killed by %s", name);
        else
            printf(": killed by signal %d", run->outcome.number);
        break;
    case SmithTestEnd_Exit:
        printf(": exited with status %d", run->outcome.number);
        break;
    case SmithTestEnd_Flood:
        printf(": wrote more than %zu MiB, which killed it", smith_test_output_limit >> 20);
        break;
    case SmithTestEnd_Passed:
    case SmithTestEnd_Check:
        break;
    }
}

// The failed check: where it is, the line as written, the values.
static void smith_test_print_check(const SmithTestRun *run) {
    if (run->outcome.end != SmithTestEnd_Check || run->line <= 0) return;
    printf("  %s:%d\n", run->file, run->line);
    char source[256];
    if (smith_test_source_line(run->file, run->line, source, sizeof source)) printf("      %s\n", source);
    smith_test_print_indented(run->detail, strlen(run->detail), "    ");
}

static void smith_test_print_failure(const SmithTest *test, const SmithTestOptions *options, const SmithTestRun *run) {
    printf("%sFAIL%s %s", smith_test_paint(options, "\033[31m"), smith_test_paint(options, "\033[0m"), test->name);
    if (test->property != nullptr) printf(" with seed %llu", (unsigned long long)run->seed);
    if (run->shrink_changed) printf(", shrunk in %u runs", run->shrink_runs);
    if (run->shrink_runs > 0 && !run->shrink_changed) printf(", no smaller case in %u runs", run->shrink_runs);
    smith_test_print_reason(run, options);
    putchar('\n');
    smith_test_print_check(run);
    if (run->note[0] != '\0') printf("    note: %s\n", run->note);
    smith_test_print_indented(run->output.text, run->output.kept, "    ");
    if (run->output.total > run->output.kept)
        printf("    ... and %zu more bytes\n", run->output.total - run->output.kept);
    if (test->property != nullptr)
        printf("    rerun: smith build test -DTEST=%s -DSEED=%llu\n", test->name, (unsigned long long)run->seed);
}

static void smith_test_report(const SmithTest *test, const SmithTestOptions *options, const SmithTestRun *run) {
    if (run->outcome.end != SmithTestEnd_Passed) {
        smith_test_print_failure(test, options, run);
    } else if (options->verbose) {
        const char *green = smith_test_paint(options, "\033[32m");
        const char *reset = smith_test_paint(options, "\033[0m");
        if (test->property == nullptr)
            printf("%sPASS%s %s\n", green, reset, test->name);
        else if (options->seed_given)
            printf("%sPASS%s %s (seed %llu)\n", green, reset, test->name, (unsigned long long)options->seed);
        else
            printf("%sPASS%s %s (%llu seeds)\n", green, reset, test->name, (unsigned long long)options->seed_count);
    }
    fflush(stdout);
}

// Shrinking.
//
// A property test draws every number from its key's words (smith_key_bits). When a key fails, the runner reruns that
// key with smith's random hook installed in the child, which records each (key, n) the test asks for, in first-use
// order, with the word it got. Then the runner tries to make the failure smaller: for each recorded word in order it
// tries 0, the simplest value of every distribution, and when the test then no longer fails it searches between 0 and
// the word for the smallest one that still does. Each try is a child with an override table that the hook reads. A try
// keeps the failure only when it is the same: the same check (file and line), or the same signal or exit status.

constexpr size_t smith_test_draws_max = 65536; // words recorded; past it, no more are
constexpr size_t smith_test_slots = 131072;    // a power of two, twice the words: the hash table is never full
constexpr unsigned smith_test_shrink_runs_max = 1000;
constexpr double smith_test_shrink_seconds_max = 10.0;

typedef struct {
    uint64_t key;
    uint64_t n;
    uint64_t word;
} SmithTestDraw;

// A set of draws with a hash table over them, in memory the runner and a child can share.
typedef struct {
    uint32_t count;
    uint32_t dropped; // words that did not fit
    SmithTestDraw draws[smith_test_draws_max];
    uint32_t slots[smith_test_slots]; // 1 + the index into draws, or 0
} SmithTestDraws;

static SmithTestDraws *smith_test_recorded;  // what the child asked for: the child writes, the runner reads
static SmithTestDraws *smith_test_overrides; // the words to give instead: the runner writes, the child reads

static uint64_t smith_test_mix(uint64_t key, uint64_t n) {
    uint64_t z = key ^ (n * 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

// The draw for (key, n): its index + 1, or 0. *slot is where it is, or where it would go.
static uint32_t smith_test_draws_find(const SmithTestDraws *table, uint64_t key, uint64_t n, size_t *slot) {
    size_t at = smith_test_mix(key, n) & (smith_test_slots - 1);
    for (size_t probe = 0; probe < smith_test_slots; probe++) {
        uint32_t entry = table->slots[at];
        if (entry == 0 || (table->draws[entry - 1].key == key && table->draws[entry - 1].n == n)) {
            *slot = at;
            return entry;
        }
        at = (at + 1) & (smith_test_slots - 1);
    }
    *slot = smith_test_slots;
    return 0;
}

// Sets the word of (key, n), adding the draw if it is new. False when the table is full.
static bool smith_test_draws_set(SmithTestDraws *table, uint64_t key, uint64_t n, uint64_t word) {
    size_t slot = 0;
    uint32_t found = smith_test_draws_find(table, key, n, &slot);
    if (found != 0) {
        table->draws[found - 1].word = word;
        return true;
    }
    if (table->count >= smith_test_draws_max) {
        table->dropped += 1;
        return false;
    }
    table->draws[table->count] = (SmithTestDraw){.key = key, .n = n, .word = word};
    table->count += 1;
    table->slots[slot] = table->count;
    return true;
}

static void smith_test_draws_clear(SmithTestDraws *table) {
    table->count = 0;
    table->dropped = 0;
    memset(table->slots, 0, sizeof table->slots);
}

// Installed in the child: gives the override for (key, n) if there is one, and records the first use with the word
// given.
static uint64_t smith_test_hook(SmithKey key, uint64_t n, uint64_t bits) {
    size_t slot = 0;
    uint32_t over = smith_test_draws_find(smith_test_overrides, key.bits, n, &slot);
    uint64_t word = over != 0 ? smith_test_overrides->draws[over - 1].word : bits;
    if (smith_test_draws_find(smith_test_recorded, key.bits, n, &slot) == 0)
        (void)smith_test_draws_set(smith_test_recorded, key.bits, n, word);
    return word;
}

// What shrinking one failure holds: the runs it keeps (the best failure so far and the latest try), and the words of
// the best one's draws.
typedef struct {
    const SmithTest *test;
    const SmithTestOptions *options;
    uint64_t seed;
    SmithTestRun *best;      // the smallest failing run so far
    SmithTestRun *candidate; // the latest try
    unsigned runs;
    double deadline;
    SmithTestDraw list[smith_test_draws_max]; // the best run's draws, in first-use order
    uint32_t count;
} SmithTestShrink;

static SmithTestRun smith_test_tries[2];
static SmithTestShrink smith_test_shrinking;

static bool smith_test_same_failure(const SmithTestRun *failure, const SmithTestRun *run) {
    if (run->outcome.end != failure->outcome.end || run->outcome.number != failure->outcome.number) return false;
    if (run->outcome.end != SmithTestEnd_Check) return true;
    return run->line == failure->line && run->file != nullptr && failure->file != nullptr &&
           strcmp(run->file, failure->file) == 0;
}

static bool smith_test_budget(const SmithTestShrink *shrink) {
    return shrink->runs < smith_test_shrink_runs_max && smith_test_seconds() < shrink->deadline;
}

// Runs the key once more as a child that records its draws, with the overrides in place. The run lands in
// shrink->candidate, with its output.
static void smith_test_try_run(SmithTestShrink *shrink) {
    SmithTestRun *run = shrink->candidate;
    SmithTestOptions options = *shrink->options;
    options.seed_given = true;
    options.seed = shrink->seed;
    smith_test_draws_clear(smith_test_recorded);
    memset(run, 0, sizeof *run);
    smith_test_hooking = true;
    SmithTestShared *shared = &smith_test_shared_slots[smith_test_jobs_max];
    smith_test_start(shrink->test, &options, shared, run);
    smith_test_hooking = false;
    while (smith_test_read(run)) {
    }
    smith_test_finish(run, shared);
    shrink->runs += 1;
}

// Tries the word for the draw at index; true (and the try becomes the best) when the failure is the same.
static bool smith_test_try_word(SmithTestShrink *shrink, uint32_t index, uint64_t word) {
    const SmithTestDraw *draw = &shrink->list[index];
    (void)smith_test_draws_set(smith_test_overrides, draw->key, draw->n, word);
    smith_test_try_run(shrink);
    bool same = smith_test_same_failure(shrink->best, shrink->candidate);
    const SmithTestDraws *recorded = smith_test_recorded;
    if (same && recorded->count > index && recorded->draws[index].key == draw->key &&
        recorded->draws[index].n == draw->n) {
        SmithTestRun *kept = shrink->best;
        shrink->best = shrink->candidate;
        shrink->candidate = kept;
        shrink->count = recorded->count;
        memcpy(shrink->list, recorded->draws, recorded->count * sizeof recorded->draws[0]);
        return true;
    }
    // Put the word back, so the table holds exactly what the best run was given.
    (void)smith_test_draws_set(smith_test_overrides, draw->key, draw->n, draw->word);
    return false;
}

// Makes the word at index as small as it can be: 0 if the failure survives it, else the smallest word between 0 and the
// word that does. True when the word got smaller.
static bool smith_test_shrink_word(SmithTestShrink *shrink, uint32_t index) {
    uint64_t high = shrink->list[index].word;
    if (high == 0 || !smith_test_budget(shrink)) return false;
    if (smith_test_try_word(shrink, index, 0)) return true;
    uint64_t low = 0; // does not fail
    bool smaller = false;
    for (int step = 0; step < 64 && high - low > 1 && smith_test_budget(shrink); step++) {
        uint64_t middle = low + (high - low) / 2;
        if (smith_test_try_word(shrink, index, middle)) {
            high = middle;
            smaller = true;
        } else {
            low = middle;
        }
    }
    return smaller;
}

// One pass over the best run's draws, in order; the list may change under it after a success.
static bool smith_test_shrink_pass(SmithTestShrink *shrink) {
    bool improved = false;
    for (uint32_t index = 0; index < shrink->count && smith_test_budget(shrink); index++)
        improved = smith_test_shrink_word(shrink, index) || improved;
    return improved;
}

// Copies the shrunk failure into the run that reports it.
static void smith_test_shrink_finish(const SmithTestShrink *shrink, bool changed, SmithTestRun *run) {
    run->shrink_runs = shrink->runs;
    run->shrink_changed = changed;
    if (!changed) return;
    run->outcome = shrink->best->outcome;
    run->file = shrink->best->file;
    run->line = shrink->best->line;
    memcpy(run->note, shrink->best->note, sizeof run->note);
    memcpy(run->detail, shrink->best->detail, sizeof run->detail);
    run->output = shrink->best->output;
}

// Whether a failed run is worth shrinking: a property test whose failure is a check, a signal or an exit status. A
// timeout would cost its whole timeout for every try.
static bool smith_test_shrinkable(const SmithTest *test, const SmithTestOptions *options, const SmithTestRun *run) {
    if (test->property == nullptr || !options->shrink) return false;
    return run->outcome.end == SmithTestEnd_Check || run->outcome.end == SmithTestEnd_Signal ||
           run->outcome.end == SmithTestEnd_Exit;
}

// Shrinks the failing key of a finished property test in place. It runs here, while the other tests' children go on;
// the same seed gives the same shrunk case unless the time budget ends the search.
static void smith_test_shrink_run(const SmithTest *test, const SmithTestOptions *options, SmithTestRun *run) {
    SmithTestShrink *shrink = &smith_test_shrinking;
    *shrink = (SmithTestShrink){.test = test,
                                .options = options,
                                .seed = run->seed,
                                .best = &smith_test_tries[0],
                                .candidate = &smith_test_tries[1],
                                .deadline = smith_test_seconds() + smith_test_shrink_seconds_max};
    smith_test_draws_clear(smith_test_overrides);
    smith_test_try_run(shrink); // the same key again, recording; it must fail as it did
    bool reproduced = smith_test_same_failure(run, shrink->candidate);
    shrink->best = shrink->candidate;
    shrink->candidate = shrink->best == &smith_test_tries[0] ? &smith_test_tries[1] : &smith_test_tries[0];
    shrink->count = smith_test_recorded->count;
    memcpy(shrink->list, smith_test_recorded->draws, shrink->count * sizeof shrink->list[0]);
    if (!reproduced) return; // it depended on earlier keys or on chance: the failure stays as it was
    bool changed = false;
    for (int pass = 0; pass < 64 && smith_test_budget(shrink); pass++) { // the budget ends it long before
        if (!smith_test_shrink_pass(shrink)) break;
        changed = true;
    }
    smith_test_shrink_finish(shrink, changed, run);
}

// The time shrinking takes counts toward the test's, for the list of the slowest.
static void smith_test_shrink(const SmithTest *test, const SmithTestOptions *options, SmithTestRun *run) {
    if (!smith_test_shrinkable(test, options, run)) return;
    double began = smith_test_seconds();
    smith_test_shrink_run(test, options, run);
    run->seconds += smith_test_seconds() - began;
}

// Running.

constexpr size_t smith_test_idle = SIZE_MAX; // a job with no test

// Everything a run needs: the tests chosen, a run for each, and what each job is running now.
typedef struct {
    const SmithTestOptions *options;
    const SmithTest **tests; // the chosen tests, in order
    SmithTestRun *runs;      // one per chosen test
    size_t count;
    SmithTestShared *shared;             // one per job
    size_t running[smith_test_jobs_max]; // the chosen test each job is running, or smith_test_idle
    struct pollfd polls[smith_test_jobs_max];
} SmithTestPlan;

// Waits until some job has output or has ended, and reads it; a job whose child has ended is finished and freed.
static void smith_test_pump(SmithTestPlan *plan) {
    size_t jobs = plan->options->jobs;
    for (size_t job = 0; job < jobs; job++) {
        bool busy = plan->running[job] != smith_test_idle;
        plan->polls[job] = (struct pollfd){.fd = busy ? plan->runs[plan->running[job]].input : -1, .events = POLLIN};
    }
    if (poll(plan->polls, (nfds_t)jobs, -1) < 0) {
        if (errno == EINTR) return;
        smith_test_die("poll");
    }
    for (size_t job = 0; job < jobs; job++) {
        if (plan->running[job] == smith_test_idle || plan->polls[job].revents == 0) continue;
        SmithTestRun *run = &plan->runs[plan->running[job]];
        if (smith_test_read(run)) continue;
        smith_test_finish(run, &plan->shared[job]);
        plan->running[job] = smith_test_idle;
    }
}

// Starts the next chosen test in a free job. Returns false if every job is busy.
static bool smith_test_launch(SmithTestPlan *plan, size_t *next) {
    for (size_t job = 0; job < plan->options->jobs; job++) {
        if (plan->running[job] != smith_test_idle) continue;
        smith_test_start(plan->tests[*next], plan->options, &plan->shared[job], &plan->runs[*next]);
        plan->running[job] = *next;
        *next += 1;
        return true;
    }
    return false;
}

static bool smith_test_any_running(const SmithTestPlan *plan) {
    for (size_t job = 0; job < plan->options->jobs; job++) {
        if (plan->running[job] != smith_test_idle) return true;
    }
    return false;
}

// Runs the chosen tests, up to jobs at once, printing each result as soon as every test before it has finished. A
// failure that is to be shrunk is held, with everything after it, until every test has finished: shrinking takes
// seconds, and while it ran nothing would read the other children's pipes or judge their timeouts, so a result would
// depend on another test. Then the held failures are shrunk and printed in order.
static void smith_test_run_all(SmithTestPlan *plan) {
    for (size_t job = 0; job < smith_test_jobs_max; job++)
        plan->running[job] = smith_test_idle;
    size_t next = 0;
    size_t printed = 0;
    for (;;) { // each pass starts a child, finishes one or reads output, and there are finitely many tests
        while (next < plan->count && smith_test_launch(plan, &next)) {
        }
        if (smith_test_any_running(plan)) smith_test_pump(plan);
        while (printed < plan->count && plan->runs[printed].finished &&
               !smith_test_shrinkable(plan->tests[printed], plan->options, &plan->runs[printed])) {
            smith_test_report(plan->tests[printed], plan->options, &plan->runs[printed]);
            printed += 1;
        }
        if (next == plan->count && !smith_test_any_running(plan)) break;
    }
    for (; printed < plan->count; printed++) { // nothing else is running now
        smith_test_shrink(plan->tests[printed], plan->options, &plan->runs[printed]);
        smith_test_report(plan->tests[printed], plan->options, &plan->runs[printed]);
    }
}

// Whether run i comes after run j when the runs are listed slowest first, the earlier of equal times first.
static bool smith_test_listed_after(const SmithTestPlan *plan, size_t i, size_t j) {
    if (plan->runs[i].seconds != plan->runs[j].seconds) return plan->runs[i].seconds < plan->runs[j].seconds;
    return i > j;
}

static void smith_test_print_slowest(const SmithTestPlan *plan) {
    constexpr size_t listed = 5;
    if (plan->count == 0) return;
    printf("\nslowest:\n");
    size_t previous = plan->count; // none yet
    for (size_t rank = 0; rank < listed && rank < plan->count; rank++) {
        size_t pick = plan->count;
        for (size_t i = 0; i < plan->count; i++) {
            if (previous != plan->count && !smith_test_listed_after(plan, i, previous)) continue;
            if (pick == plan->count || smith_test_listed_after(plan, pick, i)) pick = i;
        }
        printf("    %6.2f s  %s\n", plan->runs[pick].seconds, plan->tests[pick]->name);
        previous = pick;
    }
}

// Options.

// Parses a whole number, digits only, that fits in a uint64_t.
static bool smith_test_parse(const char *text, uint64_t *value) {
    constexpr size_t digits_max = 20; // UINT64_MAX has 20 digits
    uint64_t result = 0;
    size_t length = 0;
    for (; text[length] != '\0'; length++) {
        char digit = text[length];
        if (length == digits_max || digit < '0' || digit > '9') return false;
        uint64_t digit_value = (uint64_t)(digit - '0');
        if (result > (UINT64_MAX - digit_value) / 10) return false;
        result = result * 10 + digit_value;
    }
    if (length == 0) return false;
    *value = result;
    return true;
}

// Parses --name value into the options; false for a bad value.
static bool smith_test_parse_value(SmithTestOptions *options, const char *flag, const char *value) {
    uint64_t number = 0;
    if (strcmp(flag, "--test") == 0) {
        options->filter = value;
        return true;
    }
    if (!smith_test_parse(value, &number)) return false;
    if (strcmp(flag, "--seed") == 0) {
        options->seed = number;
        options->seed_given = true;
        return true;
    }
    if (strcmp(flag, "--seeds") == 0) {
        options->seed_count = number;
        return number > 0 && number <= smith_test_seeds_max;
    }
    if (strcmp(flag, "--jobs") == 0) {
        options->jobs = (size_t)number;
        return number > 0 && number <= smith_test_jobs_max;
    }
    if (strcmp(flag, "--timeout") == 0) {
        options->timeout = (unsigned)number;
        return number > 0 && number <= 86400;
    }
    if (strcmp(flag, "--shrink") == 0) {
        options->shrink = number == 1;
        return number <= 1;
    }
    return false;
}

static bool smith_test_parse_options(int argc, char **argv, SmithTestOptions *options) {
    for (int i = 1; i < argc; i++) {
        const char *flag = argv[i];
        if (strcmp(flag, "--list") == 0) {
            options->list = true;
        } else if (strcmp(flag, "--verbose") == 0) {
            options->verbose = true;
        } else if (i + 1 >= argc || !smith_test_parse_value(options, flag, argv[i + 1])) {
            return false;
        } else {
            i += 1; // the flag's value
        }
    }
    for (size_t i = 0; options->filter != nullptr && i < smith_test_count; i++) {
        if (strcmp(smith_test_table[i].name, options->filter) == 0) options->exact = true;
    }
    return true;
}

static bool smith_test_selects(const SmithTestOptions *options, const char *name) {
    if (options->filter == nullptr) return true;
    if (options->exact) return strcmp(name, options->filter) == 0;
    return strstr(name, options->filter) != nullptr;
}

// The tests the options choose, in order, with a run for each.
static SmithTestPlan smith_test_plan(const SmithTestOptions *options) {
    SmithTestPlan plan = {.options = options};
    plan.tests = (const SmithTest **)smith_test_map((smith_test_count + 1) * sizeof(const SmithTest *), MAP_PRIVATE);
    for (size_t i = 0; i < smith_test_count; i++) {
        const SmithTest *test = &smith_test_table[i];
        assert(test->name != nullptr && (test->plain != nullptr) != (test->property != nullptr));
        if (!smith_test_selects(options, test->name)) continue;
        plan.tests[plan.count] = test;
        plan.count += 1;
    }
    plan.runs = smith_test_map(plan.count * sizeof(SmithTestRun) + 1, MAP_PRIVATE);
    plan.shared = smith_test_map((smith_test_jobs_max + 1) * sizeof(SmithTestShared), MAP_SHARED);
    smith_test_shared_slots = plan.shared; // the last one is for shrinking
    smith_test_recorded = smith_test_map(sizeof *smith_test_recorded, MAP_SHARED);
    smith_test_overrides = smith_test_map(sizeof *smith_test_overrides, MAP_PRIVATE);
    return plan;
}

int main(int argc, char **argv) {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    SmithTestOptions options = {
        .seed_count = smith_test_seeds_default,
        .jobs = cores > 0 && (size_t)cores <= smith_test_jobs_max ? (size_t)cores : smith_test_jobs_max,
        .timeout = 10,
        .shrink = true,
        .color = isatty(STDOUT_FILENO) != 0 && getenv("NO_COLOR") == nullptr,
    };
    if (!smith_test_parse_options(argc, argv, &options)) {
        fprintf(stderr,
                "usage: %s [--test <substring>] [--seed <n>] [--seeds <1 to %llu>] [--jobs <1 to %zu>] "
                "[--timeout <seconds>] [--shrink 0|1] [--list] [--verbose]\n",
                argv[0], (unsigned long long)smith_test_seeds_max, smith_test_jobs_max);
        return smith_test_usage_status;
    }
    double start = smith_test_seconds();
    smith_test_memory = smith_test_map(smith_test_memory_capacity, MAP_PRIVATE);
    SmithTestPlan plan = smith_test_plan(&options);
    if (options.list) {
        for (size_t i = 0; i < plan.count; i++)
            puts(plan.tests[i]->name);
        return 0;
    }
    if (plan.count == 0 && options.filter != nullptr) {
        printf("no test matches \"%s\"\n", options.filter);
        return 1;
    }
    smith_test_run_all(&plan);
    size_t failed = 0;
    for (size_t i = 0; i < plan.count; i++)
        failed += plan.runs[i].outcome.end != SmithTestEnd_Passed ? 1 : 0;
    smith_test_print_slowest(&plan);
    printf("\n%zu passed, %zu failed in %.2f s\n", plan.count - failed, failed, smith_test_seconds() - start);
    return failed == 0 ? 0 : 1;
}
