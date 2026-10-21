// smith's test library: the checks' failures, the memory of a test, and the runner, which is this library's main.
// The design is in include/smith/test.h and docs/build-c.md.
//
// The runner forks a pool of workers, once each (`--jobs` of them), and sends each one the tests to run, one after
// another, in-process. A worker writes what a failure needs to know (the check's file and line, the values, the note)
// to a block of memory it shares with the runner, so it survives however the worker ends: a failed check (which unwinds
// to the worker's loop with siglongjmp), a crash or a kill. Everything the worker prints goes down a pipe, and is
// shown only when the test fails. A crash, a timeout or a sanitizer report ends a worker, and the runner forks the
// next.
// `--isolate` keeps the process-per-test runner: a child for each test, and for each SMITH_CHECK_ABORTS.
#include "../include/smith/test.h"
#include "../include/smith/trace.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef __has_feature
#define __has_feature(feature) 0
#endif

constexpr size_t smith_test_note_capacity = 4096;
constexpr size_t smith_test_detail_capacity = 1024;
constexpr size_t smith_test_text_capacity = 128;          // one formatted value
constexpr size_t smith_test_memory_capacity = 256U << 20; // every test arena of one test, or of one key, together
constexpr size_t smith_test_output_capacity = 64U << 10;  // what a failing test shows of its output
constexpr size_t smith_test_output_limit = 16U << 20;     // a child that writes more is killed
constexpr size_t smith_test_jobs_max = 256;
constexpr size_t smith_test_alternate_stack = 64U << 10;
constexpr int smith_test_failure_status = 99; // a process whose check failed exits with this
constexpr int smith_test_signal_status = 128; // a child that catches a fatal signal exits with 128 + its number
constexpr int smith_test_usage_status = 2;

// The SDK's WIFSIGNALED and WIFEXITED expand to a literal written 0177, which -Wdeprecated-octal-literals rejects under
// the C2y flags `smith new` writes, and a project compiles this file with its own flags. So the SDK's macros are used
// here only, with that one warning off.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-octal-literals"
static int smith_test_status_signal(int status) {
    return WIFSIGNALED(status) ? WTERMSIG(status) : 0;
}

static int smith_test_status_code(int status) {
    return WIFEXITED(status) ? WEXITSTATUS(status) : 0;
}
#pragma clang diagnostic pop

// What a child tells the runner, in memory they share. A block is reset before each test and each key.
typedef struct {
    const char *file; // the failed check's file: a string literal of the program, valid in every process forked from it
    int line;         // 0 when no check has failed
    uint64_t seed;    // the property test's current key
    bool aborting;    // a SMITH_CHECK_ABORTS is running in a worker, and file and line are its
    bool death_check; // a SMITH_CHECK_ABORTS has run in this test (cleared by the runner before each test)
    char note[smith_test_note_capacity];
    char detail[smith_test_detail_capacity]; // the failed check's values, as lines
    // The stack of the failure (smith_trace_capture), for the report smith builds from it.
    const char *heading; // "check failed", "crashed", "aborted": a literal of the program
    uintptr_t frames[smith_trace_frames_max];
    size_t frame_count;
    bool frames_exact; // frames[0] is the instruction a signal stopped at
} SmithTestShared;

static SmithTestShared smith_test_fallback;
static SmithTestShared *smith_test_shared = &smith_test_fallback;
static SmithTestShared *smith_test_shared_slots; // the runner's: one per job, and one more for shrinking
static unsigned char *smith_test_memory;
static size_t smith_test_memory_used; // a multiple of smith_arena_align_max
static unsigned smith_test_timeout_seconds = 10;
static bool smith_test_full_run; // the runner was given --full; set before any test forks, so every child sees it
static bool smith_test_worker;   // this process is a worker: a failed check unwinds to its loop
static sigjmp_buf smith_test_loop_jump;
static sigjmp_buf smith_test_abort_jump;
static uint64_t *smith_test_forks; // the forks of the whole run, counted in memory every process shares

static void smith_test_count_fork(void) {
    if (smith_test_forks != nullptr) __atomic_fetch_add(smith_test_forks, 1, __ATOMIC_RELAXED);
}

bool smith_test_full(void) {
    return smith_test_full_run;
}

static void smith_test_reset(void) {
    smith_test_memory_used = 0;
    smith_test_shared->file = nullptr;
    smith_test_shared->line = 0;
    smith_test_shared->note[0] = '\0';
    smith_test_shared->detail[0] = '\0';
    smith_test_shared->aborting = false;
    smith_test_shared->heading = nullptr;
    smith_test_shared->frame_count = 0;
    smith_test_shared->frames_exact = false;
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
    int wanted = vsnprintf(smith_test_shared->note, smith_test_note_capacity, format, arguments);
    va_end(arguments);
    if (wanted >= 0 && (size_t)wanted >= smith_test_note_capacity) {
        // Cut: end it with the mark, backing up so the cut does not split a UTF-8 character.
        constexpr char mark[] = "\xE2\x80\xA6"; // …
        size_t at = smith_test_note_capacity - sizeof mark;
        while (at > 0 && (((unsigned char)smith_test_shared->note[at]) & 0xC0U) == 0x80U)
            at--;
        memcpy(smith_test_shared->note + at, mark, sizeof mark);
    }
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

static uint64_t smith_test_raw_bits(SmithTestValue value) {
    assert(value.kind == SmithTestKind_Float || value.kind == SmithTestKind_Double);
    if (value.kind == SmithTestKind_Float) {
        uint32_t bits = 0;
        static_assert(sizeof bits == sizeof value.float_value);
        memcpy(&bits, &value.float_value, sizeof bits);
        return bits;
    }
    uint64_t bits = 0;
    static_assert(sizeof bits == sizeof value.double_value);
    memcpy(&bits, &value.double_value, sizeof bits);
    return bits;
}

bool smith_test_same_bits(SmithTestValue a, SmithTestValue b) {
    assert(a.kind == b.kind);
    return smith_test_raw_bits(a) == smith_test_raw_bits(b);
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

static bool smith_test_finite(SmithTestValue value) {
    if (value.kind == SmithTestKind_Float) return isfinite((double)value.float_value);
    if (value.kind == SmithTestKind_Double) return isfinite(value.double_value);
    return true;
}

typedef struct {
    const char *text; // the expression as written
    SmithTestValue value;
    char formatted[smith_test_text_capacity];
    bool literal; // the expression is the value itself, like 500
    bool bits;    // a NaN or an infinity also shows its bits, which tell two NaNs apart
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
        if (lines[i].bits && !smith_test_finite(lines[i].value)) {
            size_t length = strlen(lines[i].formatted);
            int digits = lines[i].value.kind == SmithTestKind_Float ? 8 : 16;
            snprintf(lines[i].formatted + length, smith_test_text_capacity - length, " [0x%0*llx]", digits,
                     (unsigned long long)smith_test_raw_bits(lines[i].value));
        }
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

// Where a SMITH_CHECK_ABORTS stands in a worker. Set by the macro's functions and by the signal handler.
constexpr int smith_test_abort_idle = 0;
constexpr int smith_test_abort_running = 1; // the expression is running, its output captured
constexpr int smith_test_abort_returned_state = 2;
constexpr int smith_test_abort_signaled = 3; // it raised SIGABRT or SIGTRAP, which the handler caught
constexpr int smith_test_abort_failed = 4;   // a check inside it failed
static volatile sig_atomic_t smith_test_abort_state = smith_test_abort_idle;
static volatile sig_atomic_t smith_test_abort_signal;
static volatile sig_atomic_t smith_test_death_child; // this process is the child of a SMITH_CHECK_ABORTS (--isolate)

// Ends the test with a failed check. In a worker the test unwinds to the worker's loop, unless the worker is to be
// retired (its state cannot be trusted: a sanitizer reported), and then it exits as a process-per-test child does.
[[noreturn]] static void smith_test_stop(const char *file, int line, bool retire) {
    fflush(nullptr);
    smith_test_shared->file = file;
    smith_test_shared->line = line;
    if (smith_test_shared->frame_count == 0) { // a failure that brought its own stack (smith_failure) keeps it
        smith_test_shared->heading = "check failed";
        smith_test_shared->frame_count = smith_trace_capture(smith_test_shared->frames, smith_trace_frames_max);
        smith_test_shared->frames_exact = false;
    }
    if (smith_test_worker && !retire) {
        if (smith_test_abort_state == smith_test_abort_running) {
            smith_test_abort_state = smith_test_abort_failed;
            siglongjmp(smith_test_abort_jump, 2);
        }
        siglongjmp(smith_test_loop_jump, 1);
    }
    _exit(smith_test_failure_status);
}

[[noreturn]] static void smith_test_end(const char *file, int line) {
    smith_test_stop(file, line, false);
}

void smith_test_fail_condition(const char *file, int line) {
    assert((file != nullptr) == (line > 0));
    snprintf(smith_test_shared->detail, smith_test_detail_capacity, "the condition is false\n");
    smith_test_end(file, line);
}

void smith_test_fail_compare(const char *a_text, const char *b_text, SmithTestValue a, SmithTestValue b,
                             const char *file, int line) {
    assert(a_text != nullptr && b_text != nullptr && (file != nullptr) == (line > 0));
    SmithTestLine lines[2] = {{.text = a_text, .value = a}, {.text = b_text, .value = b}};
    smith_test_write_values(lines, 2);
    smith_test_end(file, line);
}

void smith_test_fail_same_bits(const char *a_text, const char *b_text, SmithTestValue a, SmithTestValue b,
                               const char *file, int line) {
    assert(a_text != nullptr && b_text != nullptr && (file != nullptr) == (line > 0));
    SmithTestLine lines[2] = {{.text = a_text, .value = a, .bits = true}, {.text = b_text, .value = b, .bits = true}};
    smith_test_write_values(lines, 2);
    smith_test_end(file, line);
}

void smith_test_fail_near(const char *a_text, const char *b_text, const char *tolerance_text,
                          const SmithTestValue values[4], const char *file, int line) {
    assert(a_text != nullptr && b_text != nullptr && tolerance_text != nullptr && values != nullptr);
    assert((file != nullptr) == (line > 0));
    SmithTestLine lines[4] = {{.text = a_text, .value = values[0]},
                              {.text = b_text, .value = values[1]},
                              {.text = tolerance_text, .value = values[2]},
                              {.text = "distance", .value = values[3]}};
    smith_test_write_values(lines, 4);
    smith_test_end(file, line);
}

// Plain-function checks. Each takes values and no text: where it failed is the caller's line in the stack, which smith
// reads from the source file. The values print as "a" and "b", with the short name of their type.

static void smith_test_check_condition(bool condition) {
    if (!condition) smith_test_fail_condition(nullptr, 0);
}

void smith_check(bool condition) {
    smith_test_check_condition(condition);
}

// A contract or a library's check (smith_failure) failed in a test: the test fails with the stack it brought. Under a
// SMITH_CHECK_ABORTS it is the stop the check expects, as an assert's is: the program aborts (in a worker the handler
// jumps back to the check; in a child of --isolate the abort ends the child).
static void smith_test_on_failure(SmithFailureKind kind, const uintptr_t *frames, size_t count) {
    assert(frames != nullptr || count == 0);
    if (smith_test_abort_state == smith_test_abort_running || smith_test_death_child) {
        fflush(nullptr);
        abort();
    }
    smith_test_shared->heading = smith_failure_heading(kind);
    smith_test_shared->frame_count = count < smith_trace_frames_max ? count : smith_trace_frames_max;
    memcpy(smith_test_shared->frames, frames, smith_test_shared->frame_count * sizeof frames[0]);
    smith_test_shared->frames_exact = false;
    snprintf(smith_test_shared->detail, smith_test_detail_capacity, "%s\n", smith_failure_heading(kind));
    smith_test_end(nullptr, 0);
}

static SmithTestValue smith_test_typed(SmithTestValue value, const char *type) {
    value.type = type;
    return value;
}

static void smith_test_check_compare(SmithTestValue a, const char *op, SmithTestValue b) {
    if (smith_test_compare(a, op, b)) return;
    smith_test_fail_compare("a", "b", a, b, nullptr, 0);
}

// NOLINTBEGIN(bugprone-macro-parentheses): a type name cannot be parenthesized
#define SMITH_TEST_DEFINE_EQUAL(suffix, Type, make) \
    void smith_check_equal_##suffix(Type a, Type b) { \
        smith_test_check_compare(smith_test_typed(make(a), #suffix), "==", smith_test_typed(make(b), #suffix)); \
    }
// NOLINTEND(bugprone-macro-parentheses)

SMITH_TEST_DEFINE_EQUAL(bool, bool, smith_test_value_bool)
SMITH_TEST_DEFINE_EQUAL(char, char, smith_test_value_char)
SMITH_TEST_DEFINE_EQUAL(i8, int8_t, smith_test_value_signed_char)
SMITH_TEST_DEFINE_EQUAL(i16, int16_t, smith_test_value_short)
SMITH_TEST_DEFINE_EQUAL(i32, int32_t, smith_test_value_int)
SMITH_TEST_DEFINE_EQUAL(i64, int64_t, smith_test_value_long_long)
SMITH_TEST_DEFINE_EQUAL(u8, uint8_t, smith_test_value_unsigned_char)
SMITH_TEST_DEFINE_EQUAL(u16, uint16_t, smith_test_value_unsigned_short)
SMITH_TEST_DEFINE_EQUAL(u32, uint32_t, smith_test_value_unsigned_int)
SMITH_TEST_DEFINE_EQUAL(u64, uint64_t, smith_test_value_unsigned_long_long)
SMITH_TEST_DEFINE_EQUAL(f32, float, smith_test_value_float)
SMITH_TEST_DEFINE_EQUAL(f64, double, smith_test_value_double)
SMITH_TEST_DEFINE_EQUAL(ptr, const volatile void *, smith_test_value_pointer)

void smith_check_near_f32(float a, float b, float tolerance) {
    float distance = smith_test_distance_float(a, b);
    if (smith_test_within((double)distance, (double)tolerance)) return;
    SmithTestValue values[4] = {smith_test_typed(smith_test_value_float(a), "f32"),
                                smith_test_typed(smith_test_value_float(b), "f32"),
                                smith_test_typed(smith_test_value_float(tolerance), "f32"),
                                smith_test_typed(smith_test_value_float(distance), "f32")};
    smith_test_fail_near("a", "b", "tolerance", values, nullptr, 0);
}

void smith_check_near_f64(double a, double b, double tolerance) {
    double distance = smith_test_distance_double(a, b);
    if (smith_test_within(distance, tolerance)) return;
    SmithTestValue values[4] = {smith_test_typed(smith_test_value_double(a), "f64"),
                                smith_test_typed(smith_test_value_double(b), "f64"),
                                smith_test_typed(smith_test_value_double(tolerance), "f64"),
                                smith_test_typed(smith_test_value_double(distance), "f64")};
    smith_test_fail_near("a", "b", "tolerance", values, nullptr, 0);
}

void smith_check_same_bits_f32(float a, float b) {
    SmithTestValue value_a = smith_test_typed(smith_test_value_float(a), "f32");
    SmithTestValue value_b = smith_test_typed(smith_test_value_float(b), "f32");
    if (!smith_test_same_bits(value_a, value_b)) smith_test_fail_same_bits("a", "b", value_a, value_b, nullptr, 0);
}

void smith_check_same_bits_f64(double a, double b) {
    SmithTestValue value_a = smith_test_typed(smith_test_value_double(a), "f64");
    SmithTestValue value_b = smith_test_typed(smith_test_value_double(b), "f64");
    if (!smith_test_same_bits(value_a, value_b)) smith_test_fail_same_bits("a", "b", value_a, value_b, nullptr, 0);
}

// SMITH_CHECK_ABORTS.
//
// In a worker the expression runs in-process with its output going to a temporary file. A SIGABRT or SIGTRAP is caught
// by the handler, which jumps back to the macro; the check then reads the file, and a sanitizer's report in it fails
// the check and retires the worker. Any other signal, an exit or a timeout ends the worker, and the runner finds the
// check in the shared block (`aborting`) and judges how it ended. With --isolate the expression runs in a forked child
// and the parent judges its end, as it always has.

typedef struct {
    pid_t child;
    int input; // the read end of the child's output
} SmithTestDeath;

static SmithTestDeath smith_test_death;

static bool smith_test_abort_fork(void) {
    int ends[2];
    if (pipe(ends) != 0) {
        perror("smith: pipe");
        abort();
    }
    fflush(nullptr); // or the child prints what is still buffered a second time
    smith_test_count_fork();
    pid_t child = fork();
    if (child < 0) {
        perror("smith: fork");
        abort();
    }
    if (child == 0) {
        smith_test_death_child = 1;
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

// What a worker's death check captured: its output goes to a file while the expression runs.
typedef struct {
    int file; // -1 until the first check
    int out;  // the real stdout and stderr, saved while the output is captured
    int err;
    struct rlimit size; // the file-size limit before the capture lowered it
} SmithTestCapture;

static SmithTestCapture smith_test_capture = {.file = -1, .out = -1, .err = -1};
static volatile sig_atomic_t smith_test_capturing;

static void smith_test_capture_start(void) {
    if (smith_test_capture.file < 0) {
        FILE *file = tmpfile();
        if (file == nullptr) {
            perror("smith: tmpfile");
            abort();
        }
        smith_test_capture.file = fileno(file);
    }
    if (ftruncate(smith_test_capture.file, 0) != 0 || lseek(smith_test_capture.file, 0, SEEK_SET) != 0) {
        perror("smith: capture");
        abort();
    }
    fflush(nullptr);
    // A flood stops at the limit the child of --isolate is killed at: a write past it fails, SIGXFSZ being ignored.
    getrlimit(RLIMIT_FSIZE, &smith_test_capture.size);
    struct rlimit lowered = smith_test_capture.size;
    if (lowered.rlim_max > smith_test_output_limit) {
        lowered.rlim_cur = smith_test_output_limit;
        setrlimit(RLIMIT_FSIZE, &lowered);
    }
    smith_test_capture.out = dup(STDOUT_FILENO);
    smith_test_capture.err = dup(STDERR_FILENO);
    dup2(smith_test_capture.file, STDOUT_FILENO);
    dup2(smith_test_capture.file, STDERR_FILENO);
    smith_test_capturing = 1;
}

// Gives the real output back. Safe to call from a signal handler, and when no capture is on.
static void smith_test_capture_stop(void) {
    if (!smith_test_capturing) return;
    smith_test_capturing = 0;
    dup2(smith_test_capture.out, STDOUT_FILENO);
    dup2(smith_test_capture.err, STDERR_FILENO);
    close(smith_test_capture.out);
    close(smith_test_capture.err);
    setrlimit(RLIMIT_FSIZE, &smith_test_capture.size);
}

// What the expression wrote: its first capacity bytes, read after the capture has stopped.
static size_t smith_test_capture_read(char *text, size_t capacity) {
    ssize_t got = pread(smith_test_capture.file, text, capacity, 0);
    return got > 0 ? (size_t)got : 0;
}

// For a worker about to die while a check runs: stops the capture and writes what it holds to the real stderr, so the
// runner shows it with the failure. Only calls that are safe in a signal handler.
static void smith_test_capture_forward(void) {
    if (!smith_test_capturing) return;
    smith_test_capture_stop();
    char chunk[4096];
    off_t at = 0;
    for (size_t reads = 0; reads < smith_test_output_capacity / sizeof chunk; reads++) {
        ssize_t got = pread(smith_test_capture.file, chunk, sizeof chunk, at);
        if (got <= 0) break;
        (void)!write(STDERR_FILENO, chunk, (size_t)got);
        at += got;
    }
}

bool smith_test_abort_child(const char *file, int line) {
    assert(file != nullptr && line > 0);
    if (!smith_test_worker) return smith_test_abort_fork();
    smith_test_shared->file = file;
    smith_test_shared->line = line;
    smith_test_shared->aborting = true;
    smith_test_shared->death_check = true;
    smith_test_capture_start();
    smith_test_abort_state = smith_test_abort_running; // a timeout ends the worker: it is the test's, as it always was
    return true;
}

void smith_test_abort_returned(void) {
    if (smith_test_worker) {
        smith_test_abort_state = smith_test_abort_returned_state;
        return;
    }
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

static const char *smith_test_signal_name(int signal_number);

// Whether the output holds a sanitizer's report.
static bool smith_test_has_report(const char *text, size_t length) {
    static const char *const markers[] = {"AddressSanitizer", "UndefinedBehaviorSanitizer", "LeakSanitizer",
                                          "ThreadSanitizer", "runtime error:"};
    for (size_t i = 0; i < sizeof markers / sizeof markers[0]; i++) {
        size_t marker = strlen(markers[i]);
        for (size_t at = 0; at + marker <= length; at++)
            if (memcmp(text + at, markers[i], marker) == 0) return true;
    }
    return false;
}

// A wait status as the signal that ended the process (or 0) and its exit status (or 0). A process that caught a fatal
// signal exited with 128 plus its number, which counts as the signal.
static void smith_test_status_split(int status, int *signal_number, int *code) {
    *signal_number = smith_test_status_signal(status);
    *code = smith_test_status_code(status);
    if (*code > smith_test_signal_status) *signal_number = *code - smith_test_signal_status;
}

// Why the end of an expression is not the stop a death test expects, written into message, or nullptr when it is:
// SIGABRT or SIGTRAP (an assert, abort(), __builtin_trap, the arena's capacity stop) and no sanitizer report in the
// output. A sanitizer report, any other signal and any other exit are bugs.
static const char *smith_test_death_verdict(int signal_number, int code, const char *text, size_t length, char *message,
                                            size_t capacity) {
    if (signal_number == SIGALRM) return "expected an abort; it timed out";
    if (smith_test_has_report(text, length))
        return "expected an abort; it was stopped by a sanitizer report, which is a bug, not the stop the test "
               "expected";
    if (signal_number == SIGABRT || signal_number == SIGTRAP) return nullptr;
    if (signal_number != 0) {
        const char *name = smith_test_signal_name(signal_number);
        if (name != nullptr)
            snprintf(message, capacity, "expected an abort; it crashed with %s, which is not an abort", name);
        else
            snprintf(message, capacity, "expected an abort; it crashed with signal %d, which is not an abort",
                     signal_number);
        return message;
    }
    if (code == 0) return "expected an abort; it returned";
    if (code == smith_test_failure_status) return "expected an abort; a check inside it failed";
    snprintf(message, capacity, "expected an abort; it exited with status %d, which is not an abort", code);
    return message;
}

static void smith_test_abort_judge_child(const char *file, int line) {
    static char text[smith_test_output_capacity];
    size_t kept = smith_test_death_read(text, sizeof text);
    int status = 0;
    if (waitpid(smith_test_death.child, &status, 0) != smith_test_death.child) {
        perror("smith: waitpid");
        abort();
    }
    int signal_number = 0;
    int code = 0;
    smith_test_status_split(status, &signal_number, &code);
    char message[160];
    const char *verdict = smith_test_death_verdict(signal_number, code, text, kept, message, sizeof message);
    if (verdict == nullptr) return;
    if (kept > 0) fwrite(text, 1, kept, stderr); // the child's output is shown for a failure only
    snprintf(smith_test_shared->detail, smith_test_detail_capacity, "%s\n", verdict);
    smith_test_end(file, line);
}

// A worker's judgment of the expression it ran in-process: how it ended, and what it printed.
static void smith_test_abort_judge_worker(const char *file, int line) {
    int state = smith_test_abort_state;
    assert(state != smith_test_abort_idle && state != smith_test_abort_running);
    int signal_number = state == smith_test_abort_signaled ? (int)smith_test_abort_signal : 0;
    int code = state == smith_test_abort_failed ? smith_test_failure_status : 0;
    fflush(nullptr);
    smith_test_abort_state = smith_test_abort_idle;
    smith_test_capture_stop();
    smith_test_shared->aborting = false;
    static char text[smith_test_output_capacity];
    size_t kept = smith_test_capture_read(text, sizeof text);
    char message[160];
    const char *verdict = smith_test_death_verdict(signal_number, code, text, kept, message, sizeof message);
    if (verdict == nullptr) {
        smith_test_shared->file = nullptr;
        smith_test_shared->line = 0;
        return;
    }
    if (kept > 0) fwrite(text, 1, kept, stderr);
    snprintf(smith_test_shared->detail, smith_test_detail_capacity, "%s\n", verdict);
    smith_test_stop(file, line, smith_test_has_report(text, kept)); // after a report the worker is not trusted
}

void smith_test_abort_judge(const char *file, int line) {
    assert(file != nullptr && line > 0);
    if (smith_test_worker)
        smith_test_abort_judge_worker(file, line);
    else
        smith_test_abort_judge_child(file, line);
}

sigjmp_buf *smith_test_abort_jump_buffer(void) {
    return &smith_test_abort_jump;
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
    unsigned shrink_seconds; // the time shrinking one failure may take
    bool full;
    bool list;
    bool verbose;
    bool color;
    bool isolate;       // a process for each test, as before the workers
    bool in_process;    // every test in the runner's own process, for a debugger: no fork, no timeout, no shrinking
    bool timeout_given; // --timeout was passed
    bool trace;         // print a failure's stack as a trace block, for smith to turn into the report
    bool stats;         // print the forks the run made
    bool shuffle_given; // run the tests in an order drawn from shuffle_seed
    uint64_t shuffle_seed;
} SmithTestOptions;

constexpr uint64_t smith_test_seeds_default = 100;
constexpr uint64_t smith_test_seeds_max = 1'000'000'000;
constexpr uint64_t smith_test_seed_root = 0x5eed;    // the key the properties' seeds are drawn from
constexpr size_t smith_test_commands_max = 1U << 30; // what a worker serves before it exits and is forked again

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
    size_t total; // bytes the test wrote
} SmithTestOutput;

// One test's run: filled in as its process runs and ends, and printed once every test before it has been.
typedef struct {
    double start;
    double seconds;
    bool finished;
    SmithTestOutcome outcome;
    uint64_t seed; // a property test's key when the test ended
    const char *file;
    int line;
    char note[smith_test_note_capacity];
    char detail[smith_test_detail_capacity];
    const char *heading; // of the stack: see SmithTestShared
    uintptr_t frames[smith_trace_frames_max];
    size_t frame_count;
    bool frames_exact;
    SmithTestOutput output;
    unsigned shrink_runs; // the runs shrinking took, 0 when it did not run
    bool shrink_changed;  // it found a smaller failure
    bool shrink_leaked;   // the smaller failure did not reproduce in a fresh worker: attempts leaked state
} SmithTestRun;

// What the runner sends a worker: the test to run, and the one key to run a property test on, if any.
typedef struct {
    uint32_t test; // the row of smith_test_table
    bool seed_given;
    uint64_t seed;
} SmithTestCommand;

// What a worker sends back when a test ended in it without ending the worker: a pass or a failed check. A worker
// that ended some other way sends nothing, and its process's end is the runner's answer.
typedef struct {
    SmithTestEnd end;
} SmithTestReply;

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

// A process that catches a fatal signal exits with 128 plus its number, so macOS writes no crash report for a death
// test or a crashing test. AddressSanitizer keeps its own SIGSEGV, SIGBUS and SIGFPE handlers, whose report ends in
// abort(). While a worker runs a SMITH_CHECK_ABORTS, SIGABRT and SIGTRAP are what the check expects: they jump back to
// it.
static void smith_test_on_signal(int signal_number, siginfo_t *info, void *context) {
    (void)info;
    if (smith_test_abort_state == smith_test_abort_running) {
        if (signal_number == SIGABRT || signal_number == SIGTRAP) {
            smith_test_abort_signal = signal_number;
            smith_test_abort_state = smith_test_abort_signaled;
            siglongjmp(smith_test_abort_jump, 1);
        }
        smith_test_capture_forward();
    }
    smith_test_shared->heading = signal_number == SIGABRT || signal_number == SIGTRAP ? "aborted" : "crashed";
    smith_test_shared->frame_count =
        smith_trace_capture_signal(smith_test_shared->frames, smith_trace_frames_max, context);
    smith_test_shared->frames_exact = true;
    _exit(smith_test_signal_status + signal_number);
}

static void smith_test_catch_signals(void) {
    static unsigned char stack[smith_test_alternate_stack];
    stack_t alternate = {.ss_sp = stack, .ss_size = sizeof stack};
    sigaltstack(&alternate, nullptr);
    struct sigaction action = {.sa_sigaction = smith_test_on_signal, .sa_flags = SA_ONSTACK | SA_SIGINFO};
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
static bool smith_test_hooking; // the next process forked installs the hook

// Runs the test: once, or a property test on its one key or on all of them. A failure's stack ends in this function's
// frame (smith_trace_stop), so it is the same whichever way the runner reached it, which shrinking compares.
[[gnu::noinline]] static void smith_test_execute(const SmithTest *test, const SmithTestOptions *options,
                                                 const SmithTestCommand *command) {
    smith_trace_stop = (uintptr_t)__builtin_frame_address(0);
    if (test->plain != nullptr) {
        smith_test_reset();
        alarm(options->timeout);
        test->plain();
    } else if (command->seed_given) {
        smith_test_run_key(test, options, command->seed);
    } else {
        SmithKey root = smith_key_make(smith_test_seed_root);
        for (uint64_t i = 0; i < options->seed_count; i++)
            smith_test_run_key(test, options, smith_key_bits(root, i));
    }
    smith_trace_stop = 0;
}

// In a forked process, before its first test: all output goes down the pipe.
static void smith_test_setup(const SmithTestOptions *options, SmithTestShared *shared, int output) {
    smith_test_shared = shared;
    smith_test_timeout_seconds = options->timeout;
    smith_test_catch_signals();
    smith_failure_hook = smith_test_on_failure;
    if (smith_test_hooking) smith_random_hook = smith_test_hook;
    if (dup2(output, STDOUT_FILENO) < 0 || dup2(output, STDERR_FILENO) < 0) _exit(smith_test_usage_status);
    close(output);
    setvbuf(stdout, nullptr, _IONBF, 0);
}

// --isolate: the process runs its one test and exits 0 if it returns.
[[noreturn]] static void smith_test_child(const SmithTest *test, const SmithTestOptions *options,
                                          SmithTestShared *shared, int output, const SmithTestCommand *command) {
    smith_test_setup(options, shared, output);
    smith_test_execute(test, options, command);
    fflush(nullptr);
    _exit(0);
}

// A worker's test: the end it came to without the worker ending. The arenas, the note and the alarm are the test's own
// (smith_test_execute resets them before each test and each key); a failed check lands back here.
static SmithTestEnd smith_test_serve(const SmithTestOptions *options, const SmithTestCommand *command) {
    const SmithTest *test = &smith_test_table[command->test];
    int jumped = sigsetjmp(smith_test_loop_jump, 1);
    if (jumped == 0) smith_test_execute(test, options, command);
    smith_test_capture_stop();
    smith_test_abort_state = smith_test_abort_idle;
    alarm(0); // an idle worker is not on the clock
    fflush(nullptr);
    setvbuf(stdout, nullptr, _IONBF, 0); // a test may have given stdout a buffer of its own
    return jumped == 0 ? SmithTestEnd_Passed : SmithTestEnd_Check;
}

// A worker that exits while a death check runs gives the output the check held back to the runner.
static void smith_test_on_exit(void) {
    if (smith_test_abort_state == smith_test_abort_running) {
        fflush(nullptr);
        smith_test_capture_forward();
    }
}

// A worker's life: it takes a command, runs the test, and answers, until the runner closes the socket.
[[noreturn]] static void smith_test_worker_main(const SmithTestOptions *options, SmithTestShared *shared, int control,
                                                int output) {
    smith_test_worker = true;
    signal(SIGPIPE, SIG_DFL);
    signal(SIGXFSZ, SIG_IGN);
    smith_test_setup(options, shared, output);
    atexit(smith_test_on_exit);
    for (size_t served = 0; served < smith_test_commands_max; served++) {
        SmithTestCommand command;
        if (read(control, &command, sizeof command) != (ssize_t)sizeof command) break;
        SmithTestReply reply = {.end = smith_test_serve(options, &command)};
        if (write(control, &reply, sizeof reply) != (ssize_t)sizeof reply) break;
    }
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
    int signal_number = 0;
    int code = 0;
    smith_test_status_split(status, &signal_number, &code);
    if (signal_number == SIGALRM) return (SmithTestOutcome){.end = SmithTestEnd_Timeout, .number = SIGALRM};
    if (signal_number != 0) return (SmithTestOutcome){.end = SmithTestEnd_Signal, .number = signal_number};
    if (code == 0) return (SmithTestOutcome){.end = SmithTestEnd_Passed};
    if (code == smith_test_failure_status) return (SmithTestOutcome){.end = SmithTestEnd_Check};
    return (SmithTestOutcome){.end = SmithTestEnd_Exit, .number = code};
}

// The processes. A job is a slot; the last slot is for shrinking, and its worker installs the random hook. With
// --isolate a slot holds the process of one test, and has no control socket.
typedef struct {
    pid_t pid;   // 0 when the slot has no process
    int control; // the runner's end of the socket the commands and replies go over; -1 with --isolate
    int output;  // the read end of the process's output
    bool output_eof;
} SmithTestWorker;

static SmithTestWorker smith_test_workers[smith_test_jobs_max + 1];
constexpr size_t smith_test_shrink_slot = smith_test_jobs_max;

static void smith_test_close_worker(SmithTestWorker *worker) {
    if (worker->control >= 0) close(worker->control);
    if (worker->output >= 0) close(worker->output);
    worker->control = -1;
    worker->output = -1;
}

// Ends a worker that is idle, or is of no use: its process is killed and reaped.
static void smith_test_discard(SmithTestWorker *worker) {
    if (worker->pid == 0) return;
    kill(worker->pid, SIGKILL);
    smith_test_close_worker(worker);
    int status = 0;
    waitpid(worker->pid, &status, 0);
    worker->pid = 0;
}

// In a new process: the other jobs' descriptors are not its to hold, or the runner closing them would not end them.
static void smith_test_close_others(size_t slot) {
    for (size_t other = 0; other <= smith_test_jobs_max; other++) {
        if (other != slot && smith_test_workers[other].pid != 0) smith_test_close_worker(&smith_test_workers[other]);
    }
}

// Forks the slot's worker, which serves commands until its socket closes.
static void smith_test_spawn(const SmithTestOptions *options, size_t slot) {
    int output[2];
    int control[2];
    if (pipe(output) != 0) smith_test_die("pipe");
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, control) != 0) smith_test_die("socketpair");
    fflush(nullptr);
    smith_test_count_fork();
    smith_test_hooking = slot == smith_test_shrink_slot;
    pid_t child = fork();
    if (child < 0) smith_test_die("fork");
    if (child == 0) {
        close(output[0]);
        close(control[0]);
        smith_test_close_others(slot);
        smith_test_worker_main(options, &smith_test_shared_slots[slot], control[1], output[1]);
    }
    smith_test_hooking = false;
    close(output[1]);
    close(control[1]);
    // NOLINTNEXTLINE(bugprone-signed-bitwise): fcntl's O_* flags are signed ints defined by the system header
    if (fcntl(output[0], F_SETFL, O_NONBLOCK) != 0) smith_test_die("fcntl");
    smith_test_workers[slot] = (SmithTestWorker){.pid = child, .control = control[0], .output = output[0]};
}

// --isolate: forks the test's child, its output going down a pipe the runner reads.
static void smith_test_start_isolated(const SmithTest *test, const SmithTestOptions *options, size_t slot,
                                      const SmithTestCommand *command) {
    int ends[2];
    if (pipe(ends) != 0) smith_test_die("pipe");
    fflush(nullptr);
    smith_test_count_fork();
    smith_test_hooking = slot == smith_test_shrink_slot;
    pid_t child = fork();
    if (child < 0) smith_test_die("fork");
    if (child == 0) {
        close(ends[0]);
        smith_test_child(test, options, &smith_test_shared_slots[slot], ends[1], command);
    }
    smith_test_hooking = false;
    close(ends[1]);
    smith_test_workers[slot] = (SmithTestWorker){.pid = child, .control = -1, .output = ends[0]};
}

// Starts the test in the slot: a command to its worker (forked first if it has none), or its own child with --isolate.
static void smith_test_dispatch(const SmithTestOptions *options, size_t slot, const SmithTest *test,
                                SmithTestRun *run) {
    memset(&smith_test_shared_slots[slot], 0, sizeof smith_test_shared_slots[slot]);
    SmithTestCommand command = {
        .test = (uint32_t)(test - smith_test_table), .seed_given = options->seed_given, .seed = options->seed};
    if (options->isolate) {
        smith_test_start_isolated(test, options, slot, &command);
        run->start = smith_test_seconds();
        return;
    }
    for (int attempt = 0; attempt < 2; attempt++) { // a worker that died while idle is replaced once
        SmithTestWorker *worker = &smith_test_workers[slot];
        if (worker->pid == 0) smith_test_spawn(options, slot);
        run->start = smith_test_seconds();
        if (write(worker->control, &command, sizeof command) == (ssize_t)sizeof command) return;
        smith_test_discard(worker);
    }
    smith_test_die("write");
}

typedef enum : unsigned char {
    SmithTestRead_Data,
    SmithTestRead_Empty,
    SmithTestRead_Closed, // the process closed its output, or wrote more than smith_test_output_limit and was killed
} SmithTestRead;

// Reads what is waiting from the process, keeping the start of it.
static SmithTestRead smith_test_read(SmithTestWorker *worker, SmithTestRun *run) {
    SmithTestOutput *output = &run->output;
    char chunk[4096];
    ssize_t got = read(worker->output, chunk, sizeof chunk);
    if (got < 0) return errno == EAGAIN || errno == EINTR ? SmithTestRead_Empty : SmithTestRead_Closed;
    if (got == 0) return SmithTestRead_Closed;
    size_t room = smith_test_output_capacity - output->kept;
    size_t keep = (size_t)got < room ? (size_t)got : room;
    memcpy(output->text + output->kept, chunk, keep);
    output->kept += keep;
    output->total += (size_t)got;
    if (output->total < smith_test_output_limit) return SmithTestRead_Data;
    kill(worker->pid, SIGKILL);
    return SmithTestRead_Closed;
}

// Everything the process wrote before it answered or died is in the pipe: take it, so it is this test's output.
static void smith_test_drain(SmithTestWorker *worker, SmithTestRun *run) {
    for (size_t reads = 0; reads <= smith_test_output_limit / 4096 + 1 && !worker->output_eof; reads++) {
        SmithTestRead got = smith_test_read(worker, run);
        if (got == SmithTestRead_Closed) worker->output_eof = true;
        if (got != SmithTestRead_Data) break;
    }
}

// A worker that died while a SMITH_CHECK_ABORTS ran: the check is where it failed, and how the worker ended says why.
static void smith_test_judge_death_check(SmithTestRun *run, int status) {
    int signal_number = 0;
    int code = 0;
    smith_test_status_split(status, &signal_number, &code);
    char message[160];
    const char *verdict =
        smith_test_death_verdict(signal_number, code, run->output.text, run->output.kept, message, sizeof message);
    if (verdict == nullptr) return;
    run->outcome = (SmithTestOutcome){.end = SmithTestEnd_Check};
    snprintf(run->detail, sizeof run->detail, "%s\n", verdict);
}

// Whether a worker that answered is not to run another test. A death check stands for the program ending, so the worker
// that ran one ends too, whether it passed or failed. A failed check siglongjmps out of arbitrary code and can leave
// state half-done, so a failed test ends its worker; the shrinking worker is the exception, since it reruns one key and
// its attempts mostly fail. A sanitizer's report in the output means the state is not trusted. Only a clean pass with
// no death check is followed by another test.
static bool smith_test_retire(size_t slot, const SmithTestRun *run, const SmithTestShared *shared) {
    if (shared->death_check || smith_test_has_report(run->output.text, run->output.kept)) return true;
    return run->outcome.end != SmithTestEnd_Passed && slot != smith_test_shrink_slot;
}

// The test has ended, by the worker's answer or (reply is nullptr) by the process's end: judge it, and copy what it
// left in the shared block to the run.
static void smith_test_finish(size_t slot, SmithTestRun *run, const SmithTestReply *reply) {
    SmithTestWorker *worker = &smith_test_workers[slot];
    const SmithTestShared *shared = &smith_test_shared_slots[slot];
    smith_test_drain(worker, run);
    int status = 0;
    if (reply == nullptr) {
        smith_test_close_worker(worker);
        if (waitpid(worker->pid, &status, 0) != worker->pid) smith_test_die("waitpid");
        worker->pid = 0;
        run->outcome = smith_test_judge(status, run->output.total >= smith_test_output_limit);
    } else {
        run->outcome = (SmithTestOutcome){.end = reply->end};
    }
    run->seconds = smith_test_seconds() - run->start;
    run->seed = shared->seed;
    run->file = shared->file;
    run->line = shared->line;
    memcpy(run->note, shared->note, sizeof run->note);
    memcpy(run->detail, shared->detail, sizeof run->detail);
    run->heading = shared->heading;
    run->frame_count = shared->frame_count;
    run->frames_exact = shared->frames_exact;
    memcpy(run->frames, shared->frames, sizeof run->frames);
    if (reply == nullptr && shared->aborting && shared->line > 0 && run->outcome.end != SmithTestEnd_Flood &&
        run->outcome.end != SmithTestEnd_Timeout) {
        smith_test_judge_death_check(run, status);
        run->frame_count = 0; // the stack is of the crash inside the expression; the check is where the report goes
    }
    if (reply != nullptr && smith_test_retire(slot, run, shared)) smith_test_discard(worker);
    run->finished = true;
}

// What a poll said about a busy slot. True once its test has finished.
static bool smith_test_service(size_t slot, SmithTestRun *run, short output_events, short control_events) {
    SmithTestWorker *worker = &smith_test_workers[slot];
    if (worker->control < 0) { // --isolate: the test is over when its output ends
        if (smith_test_read(worker, run) != SmithTestRead_Closed) return false;
        smith_test_finish(slot, run, nullptr);
        return true;
    }
    if (output_events != 0 && !worker->output_eof && smith_test_read(worker, run) == SmithTestRead_Closed)
        worker->output_eof = true;
    if (control_events == 0) return false;
    SmithTestReply reply;
    ssize_t got = read(worker->control, &reply, sizeof reply);
    if (got < 0 && errno == EINTR) return false;
    smith_test_finish(slot, run, got == (ssize_t)sizeof reply ? &reply : nullptr);
    return true;
}

// Waits for the test in the slot to finish; nothing else runs meanwhile.
static void smith_test_wait(size_t slot, SmithTestRun *run) {
    SmithTestWorker *worker = &smith_test_workers[slot];
    for (;;) { // the test ends, or dies, or its alarm kills it
        struct pollfd polls[2] = {{.fd = worker->output_eof ? -1 : worker->output, .events = POLLIN},
                                  {.fd = worker->control, .events = POLLIN}};
        if (poll(polls, 2, -1) < 0) {
            if (errno == EINTR) continue;
            smith_test_die("poll");
        }
        if (smith_test_service(slot, run, polls[0].revents, polls[1].revents)) return;
    }
}

// Ends the workers, which are idle: closing a socket ends its worker.
static void smith_test_stop_workers(void) {
    for (size_t slot = 0; slot <= smith_test_jobs_max; slot++) {
        SmithTestWorker *worker = &smith_test_workers[slot];
        if (worker->pid == 0) continue;
        smith_test_close_worker(worker);
        int status = 0;
        waitpid(worker->pid, &status, 0);
        worker->pid = 0;
    }
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

static void smith_test_print_reason(const SmithTestRun *run) {
    const char *name = smith_test_signal_name(run->outcome.number);
    switch (run->outcome.end) {
    case SmithTestEnd_Timeout:
        printf(": timed out after %.1f s", run->seconds);
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
}

// The values of the failed check, under where it failed.
static void smith_test_print_detail(const SmithTestRun *run) {
    smith_test_print_indented(run->detail, strlen(run->detail), "    ");
}

// The failure's stack as a trace block, for smith to turn into the failing line and the call chain (smith symbolize).
// The block is written to the descriptor, so what stdio holds is flushed first. A failure that knows its file and line
// (the old macros) puts them in the block as its site, which the report falls back on when the stack has no debug info.
static void smith_test_print_trace(const SmithTestRun *run, bool with_detail) {
    char site[512] = "";
    if (run->line > 0 && run->file != nullptr) snprintf(site, sizeof site, "%s:%d", run->file, run->line);
    const char *heading = run->heading != nullptr ? run->heading : "check failed";
    fflush(stdout);
    smith_trace_write(STDOUT_FILENO, "  ", heading, site[0] != '\0' ? site : nullptr,
                      with_detail ? run->detail : nullptr, run->frames, run->frame_count, run->frames_exact, true);
}

// Where it failed: the check's file and line and the line as written, or (for a plain-function check, a crash, or
// when smith is to read the stack) the trace block. True when the block carries the check's values, which smith prints
// under the source line, so they are not printed again.
static bool smith_test_print_where(const SmithTestOptions *options, const SmithTestRun *run) {
    bool check = run->outcome.end == SmithTestEnd_Check;
    bool crash = run->outcome.end == SmithTestEnd_Signal;
    bool stack = run->frame_count > 0 || (check && run->line > 0);
    if (check && run->line > 0 && !(options->trace && stack)) {
        smith_test_print_check(run);
        return false;
    }
    if (!(check || crash) || !stack) return false;
    if (crash && !options->trace) return false; // the reason after FAIL says it; the stack is for smith to read
    if (crash && smith_test_has_report(run->output.text, run->output.kept))
        return false; // the sanitizer's report is the stack
    smith_test_print_trace(run, check && options->trace);
    return check && options->trace;
}

static void smith_test_print_failure(const SmithTest *test, const SmithTestOptions *options, const SmithTestRun *run) {
    printf("%sFAIL%s %s", smith_test_paint(options, "\033[31m"), smith_test_paint(options, "\033[0m"), test->name);
    if (test->property != nullptr) printf(" with seed %llu", (unsigned long long)run->seed);
    if (run->shrink_changed) printf(", shrunk in %u runs", run->shrink_runs);
    if (run->shrink_runs > 0 && !run->shrink_changed) printf(", no smaller case in %u runs", run->shrink_runs);
    smith_test_print_reason(run);
    putchar('\n');
    bool detailed = smith_test_print_where(options, run);
    if (run->outcome.end == SmithTestEnd_Check && !detailed) smith_test_print_detail(run);
    if (run->shrink_leaked)
        printf("    the shrunk case did not reproduce in a fresh process, so state leaked between the shrinking "
               "attempts; this is the failure as it was\n");
    if (run->note[0] != '\0') printf("    note: %s\n", run->note);
    smith_test_print_indented(run->output.text, run->output.kept, "    ");
    if (run->output.total > run->output.kept)
        printf("    ... and %zu more bytes\n", run->output.total - run->output.kept);
    if (test->property != nullptr)
        printf("    rerun: smith test --test %s --seed %llu\n", test->name, (unsigned long long)run->seed);
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
// key in a worker with smith's random hook installed, which records each (key, n) the test asks for, in first-use
// order, with the word it got. Then the runner tries to make the failure smaller: for each recorded word in order it
// tries 0, the simplest value of every distribution, and when the test then no longer fails it searches between 0 and
// the word for the smallest one that still does. Each try is a run in that worker (a fork only when the try kills it),
// with an override table that the hook reads. A try keeps the failure only when it is the same: the same check (file
// and line), or the same signal or exit status.

constexpr size_t smith_test_draws_max = 65536; // words recorded; past it, no more are
constexpr size_t smith_test_slots = 131072;    // a power of two, twice the words: the hash table is never full
constexpr unsigned smith_test_shrink_runs_max = 1000;
constexpr unsigned smith_test_shrink_seconds_default = 10;

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
static SmithTestDraws *smith_test_overrides; // the words to give instead: the runner writes, the worker reads

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
    if (run->file == nullptr || failure->file == nullptr) { // a plain function check: the same stack is the same check
        // The last frame is in smith_test_execute, whose call to a property test is at one place for all keys and at
        // another for one key, so it is not compared.
        size_t compared = run->frame_count > 0 ? run->frame_count - 1 : 0;
        return run->frame_count == failure->frame_count && compared > 0 &&
               memcmp(run->frames, failure->frames, compared * sizeof run->frames[0]) == 0;
    }
    return run->line == failure->line && strcmp(run->file, failure->file) == 0;
}

static bool smith_test_budget(const SmithTestShrink *shrink) {
    return shrink->runs < smith_test_shrink_runs_max && smith_test_seconds() < shrink->deadline;
}

// Runs the key once more in the shrinking worker, which records its draws, with the overrides in place. The run lands
// in shrink->candidate, with its output.
static void smith_test_try_run(SmithTestShrink *shrink) {
    SmithTestRun *run = shrink->candidate;
    SmithTestOptions options = *shrink->options;
    options.seed_given = true;
    options.seed = shrink->seed;
    smith_test_draws_clear(smith_test_recorded);
    memset(run, 0, sizeof *run);
    smith_test_dispatch(&options, smith_test_shrink_slot, shrink->test, run);
    smith_test_wait(smith_test_shrink_slot, run);
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
static void smith_test_shrink_finish(const SmithTestShrink *shrink, bool changed, bool leaked, SmithTestRun *run) {
    run->shrink_runs = shrink->runs;
    run->shrink_changed = changed;
    run->shrink_leaked = leaked;
    if (!changed) return;
    run->outcome = shrink->best->outcome;
    run->file = shrink->best->file;
    run->line = shrink->best->line;
    memcpy(run->note, shrink->best->note, sizeof run->note);
    memcpy(run->detail, shrink->best->detail, sizeof run->detail);
    run->heading = shrink->best->heading;
    run->frame_count = shrink->best->frame_count;
    run->frames_exact = shrink->best->frames_exact;
    memcpy(run->frames, shrink->best->frames, sizeof run->frames);
    run->output = shrink->best->output;
}

// Whether a failed run is worth shrinking: a property test whose failure is a check, a signal or an exit status. A
// timeout would cost its whole timeout for every try.
static bool smith_test_shrinkable(const SmithTest *test, const SmithTestOptions *options, const SmithTestRun *run) {
    if (test->property == nullptr || !options->shrink) return false;
    return run->outcome.end == SmithTestEnd_Check || run->outcome.end == SmithTestEnd_Signal ||
           run->outcome.end == SmithTestEnd_Exit;
}

// The shrinking worker is reused across failing attempts, so it may be dirty: a shrunk case could fail only because of
// state an earlier attempt left. Reruns the best case once in a fresh worker (with --isolate every attempt already was
// one) and says whether it fails the same way.
static bool smith_test_shrink_confirm(SmithTestShrink *shrink) {
    if (shrink->options->isolate) return true;
    smith_test_discard(&smith_test_workers[smith_test_shrink_slot]);
    unsigned runs = shrink->runs;
    smith_test_try_run(shrink);
    shrink->runs = runs; // the check is not one of the attempts the budget counts
    return smith_test_same_failure(shrink->best, shrink->candidate);
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
                                .deadline = smith_test_seconds() + options->shrink_seconds};
    smith_test_draws_clear(smith_test_overrides);
    smith_test_discard(&smith_test_workers[smith_test_shrink_slot]); // a worker an earlier shrink left may be dirty
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
    bool leaked = changed && !smith_test_shrink_confirm(shrink);
    smith_test_shrink_finish(shrink, changed && !leaked, leaked, run);
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

// Everything a run needs: the tests chosen, a run for each, the order they start in, and what each job is running now.
typedef struct {
    const SmithTestOptions *options;
    const SmithTest **tests; // the chosen tests, in table order
    SmithTestRun *runs;      // one per chosen test
    size_t *order;           // the chosen tests in the order they start: the table's, or shuffled
    size_t count;
    size_t running[smith_test_jobs_max]; // the chosen test each job is running, or smith_test_idle
    struct pollfd polls[2 * smith_test_jobs_max];
} SmithTestPlan;

// Waits until some job has output or has ended, and reads it; a job whose test has ended is finished and freed.
static void smith_test_pump(SmithTestPlan *plan) {
    size_t jobs = plan->options->jobs;
    for (size_t job = 0; job < jobs; job++) {
        const SmithTestWorker *worker = &smith_test_workers[job];
        bool busy = plan->running[job] != smith_test_idle;
        plan->polls[2 * job] =
            (struct pollfd){.fd = busy && !worker->output_eof ? worker->output : -1, .events = POLLIN};
        plan->polls[2 * job + 1] = (struct pollfd){.fd = busy ? worker->control : -1, .events = POLLIN};
    }
    if (poll(plan->polls, (nfds_t)(2 * jobs), -1) < 0) {
        if (errno == EINTR) return;
        smith_test_die("poll");
    }
    for (size_t job = 0; job < jobs; job++) {
        short output_events = plan->polls[2 * job].revents;
        short control_events = plan->polls[2 * job + 1].revents;
        if (plan->running[job] == smith_test_idle || (output_events == 0 && control_events == 0)) continue;
        if (smith_test_service(job, &plan->runs[plan->running[job]], output_events, control_events))
            plan->running[job] = smith_test_idle;
    }
}

// Starts the next chosen test in a free job. Returns false if every job is busy.
static bool smith_test_launch(SmithTestPlan *plan, size_t *next) {
    for (size_t job = 0; job < plan->options->jobs; job++) {
        if (plan->running[job] != smith_test_idle) continue;
        size_t chosen = plan->order[*next];
        smith_test_dispatch(plan->options, job, plan->tests[chosen], &plan->runs[chosen]);
        plan->running[job] = chosen;
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

// Runs the chosen tests, up to jobs at once, printing each result as soon as every test before it (in table order) has
// finished. A failure that is to be shrunk is held, with everything after it, until every test has finished: shrinking
// takes seconds, and while it ran nothing would read the other workers' pipes or judge their timeouts, so a result
// would depend on another test. Then the held failures are shrunk and printed in order.
static void smith_test_run_all(SmithTestPlan *plan) {
    for (size_t job = 0; job < smith_test_jobs_max; job++)
        plan->running[job] = smith_test_idle;
    size_t next = 0;
    size_t printed = 0;
    for (;;) { // each pass starts a test, finishes one or reads output, and there are finitely many tests
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
    smith_test_stop_workers();
}

// --in-process: the tests run one after another in the runner's own process, which forks nothing, so a debugger that
// does not follow forks (lldb on macOS) stops inside them. A failed check unwinds to here as it does in a worker; a
// crash, an abort outside a death check or a hang ends the whole run (there is no process to take the blow), and with
// no alarm by default a stopped test is not timed out. Output goes to the terminal as the test writes it.
static void smith_test_run_in_process(SmithTestPlan *plan) {
    const SmithTestOptions *options = plan->options;
    SmithTestShared *shared = &smith_test_shared_slots[0];
    smith_test_shared = shared;
    smith_test_timeout_seconds = options->timeout;
    smith_test_worker = true;
    smith_test_catch_signals();
    smith_failure_hook = smith_test_on_failure;
    setvbuf(stdout, nullptr, _IONBF, 0);
    for (size_t i = 0; i < plan->count; i++) {
        SmithTestRun *run = &plan->runs[plan->order[i]];
        const SmithTest *test = plan->tests[plan->order[i]];
        memset(shared, 0, sizeof *shared);
        SmithTestCommand command = {
            .test = (uint32_t)(test - smith_test_table), .seed_given = options->seed_given, .seed = options->seed};
        run->start = smith_test_seconds();
        smith_test_abort_state = smith_test_abort_idle;
        run->outcome = (SmithTestOutcome){.end = smith_test_serve(options, &command)};
        run->seconds = smith_test_seconds() - run->start;
        run->seed = shared->seed;
        run->file = shared->file;
        run->line = shared->line;
        memcpy(run->note, shared->note, sizeof run->note);
        memcpy(run->detail, shared->detail, sizeof run->detail);
        run->heading = shared->heading;
        run->frame_count = shared->frame_count;
        run->frames_exact = shared->frames_exact;
        memcpy(run->frames, shared->frames, sizeof run->frames);
        run->finished = true;
        smith_test_report(test, options, run);
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
        options->timeout_given = true;
        return number > 0 && number <= 86400;
    }
    if (strcmp(flag, "--shrink") == 0) {
        options->shrink = number == 1;
        return number <= 1;
    }
    if (strcmp(flag, "--shrink-seconds") == 0) {
        options->shrink_seconds = (unsigned)number;
        return number > 0 && number <= 86400;
    }
    if (strcmp(flag, "--shuffle") == 0) {
        options->shuffle_seed = number;
        options->shuffle_given = true;
        return true;
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
        } else if (strcmp(flag, "--full") == 0) {
            options->full = true;
        } else if (strcmp(flag, "--isolate") == 0) {
            options->isolate = true;
        } else if (strcmp(flag, "--in-process") == 0) {
            options->in_process = true;
        } else if (strcmp(flag, "--stats") == 0) {
            options->stats = true;
        } else if (strcmp(flag, "--trace") == 0) {
            options->trace = true;
        } else if (strcmp(flag, "--color") == 0) {
            options->color = true;
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

// Fisher-Yates, drawing from the seed: the same seed gives the same order. This finds a test that passes only because
// of what ran before it.
static void smith_test_shuffle(size_t *order, size_t count, uint64_t seed) {
    for (size_t i = count; i > 1; i--) {
        size_t other = (size_t)(smith_test_mix(seed, i) % i);
        size_t kept = order[i - 1];
        order[i - 1] = order[other];
        order[other] = kept;
    }
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
    plan.order = smith_test_map((plan.count + 1) * sizeof(size_t), MAP_PRIVATE);
    for (size_t i = 0; i < plan.count; i++)
        plan.order[i] = i;
    if (options->shuffle_given) smith_test_shuffle(plan.order, plan.count, options->shuffle_seed);
    smith_test_shared_slots = smith_test_map((smith_test_jobs_max + 1) * sizeof(SmithTestShared), MAP_SHARED);
    smith_test_recorded = smith_test_map(sizeof *smith_test_recorded, MAP_SHARED);
    smith_test_overrides = smith_test_map(sizeof *smith_test_overrides, MAP_SHARED);
    return plan;
}

// Each job holds two descriptors in the runner, and the soft limit can be as low as 256: raise it as far as the hard
// limit allows, so a run of many jobs is not stopped by it.
static void smith_test_raise_files(size_t jobs) {
    struct rlimit limit;
    rlim_t wanted = 2 * (rlim_t)jobs + 64;
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0 || limit.rlim_cur >= wanted) return;
    limit.rlim_cur = wanted < limit.rlim_max ? wanted : limit.rlim_max;
    setrlimit(RLIMIT_NOFILE, &limit);
}

int main(int argc, char **argv) {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    SmithTestOptions options = {
        .seed_count = smith_test_seeds_default,
        .jobs = cores > 0 && (size_t)cores <= smith_test_jobs_max ? (size_t)cores : smith_test_jobs_max,
        .timeout = 10,
        .shrink = true,
        .shrink_seconds = smith_test_shrink_seconds_default,
        .color = isatty(STDOUT_FILENO) != 0 && getenv("NO_COLOR") == nullptr,
    };
    if (!smith_test_parse_options(argc, argv, &options)) {
        fprintf(stderr,
                "usage: %s [--test <substring>] [--seed <n>] [--seeds <1 to %llu>] [--jobs <1 to %zu>] "
                "[--timeout <seconds>] [--shrink 0|1] [--shrink-seconds <seconds>] [--shuffle <seed>] [--isolate] "
                "[--in-process] "
                "[--full] [--list] [--verbose] "
                "[--stats] [--trace] [--color]\n",
                argv[0], (unsigned long long)smith_test_seeds_max, smith_test_jobs_max);
        return smith_test_usage_status;
    }
    if (options.in_process) {
        options.shrink = false; // shrinking reruns the test in workers of its own
        if (!options.timeout_given) options.timeout = 0;
    }
    double start = smith_test_seconds();
    smith_test_full_run = options.full;
    smith_test_raise_files(options.jobs);
    signal(SIGPIPE, SIG_IGN); // a worker that died while idle is a failed write, not the end of the runner
    smith_test_forks = smith_test_map(sizeof *smith_test_forks, MAP_SHARED);
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
    if (options.in_process)
        smith_test_run_in_process(&plan);
    else
        smith_test_run_all(&plan);
    size_t failed = 0;
    for (size_t i = 0; i < plan.count; i++)
        failed += plan.runs[i].outcome.end != SmithTestEnd_Passed ? 1 : 0;
    smith_test_print_slowest(&plan);
    printf("\n%zu passed, %zu failed in %.2f s\n", plan.count - failed, failed, smith_test_seconds() - start);
    if (options.stats) printf("forks: %llu\n", (unsigned long long)*smith_test_forks);
    return failed == 0 ? 0 : 1;
}
