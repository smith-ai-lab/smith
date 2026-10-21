// <smith/test.h>: tests, written once and done well. A test is a plain function in a file build.c names:
//
//   void test_addition(void) { SMITH_CHECK(2 + 2, ==, 4); }
//   void test_sorted(SmithKey key) { ... }   // a property test: it runs on 100 keys, and a failure prints its seed
//
// There is no registration: smith_tests (build.c) finds `void test_<name>(void) {` and
// `void test_<name>(SmithKey key) {` by name, and the library's main runs them, in parallel, with failures printed with
// both values and their types. `smith test` runs them; its arguments are the runner's options: --test <substring>,
// --seed <n>, --seeds <n>, --jobs <n>, --shrink 0|1, --shuffle <seed>, --isolate, --in-process and --full
// (docs/build-c.md).
//
// The runner forks one worker per job, once, and each worker runs the tests it is sent one after another, in its own
// process. A test starts with fresh arenas and an empty note; what it leaves in global variables, open files and the
// process stays for the next test that worker runs, except after these, which end the worker: a failed test (a failed
// check unwinds out of arbitrary code and can leave state half-done), a test that ran a SMITH_CHECK_ABORTS (a death
// check stands for the program ending), a crash, a timeout, an exit and a sanitizer report. The runner forks another
// worker, so a worker is reused only after a clean pass with no death check. Which tests share a worker depends on
// timing, unless there is one job. A test must not depend on another: --shuffle <seed> runs the tests in a shuffled
// order to find one that does (with --jobs 1 the order, and so the failure, replays from the seed), and --isolate
// gives every test a process of its own. Under a debugger that cannot follow a fork (lldb on macOS) use --in-process:
// the tests run in the runner's own process, one after another, with no fork (`smith dbg build/tests -- --test name
// --in-process`). It gives up the isolation: a crash, an abort outside a SMITH_CHECK_ABORTS or a hang ends the whole
// run, state a test leaves reaches the tests after it, there is no timeout unless --timeout is given, and no shrinking.
//
// A project builds the library with smith_tests and links it with smith's arena and random; no other file includes
// more than this header.
//
// The runner sets a timer for each test and for each key of a property test (`--timeout`, 10 s). A test that calls
// `alarm` itself replaces that timer, and when its alarm fires the test ends as a timeout, reported with the time it
// ran
// ("timed out after 2.0 s"). The runner clears the timer when a test ends.
//
// A test whose heavy checks have a full size reads smith_test_full(): false by default, true when the run was asked
// for it (`smith test --full`). The test picks its size from it and checks the same
// properties either way; the default run keeps `smith test` fast, and --full is the slow pass.
//
// The checks are plain functions. A failure shows the line that called the check, read from the source file, and the
// call chain that led to it: the program records the addresses of its stack (<smith/trace.h>), and smith turns them
// into files, lines and source text, so a check needs no macro to capture its text or its place. `smith test`
// prints the report; run the runner by hand and it prints the raw trace, which `smith symbolize` reads.
//
//   smith_check(condition)                 fails the test when the condition is false
//   smith_check_equal_<type>(a, b)         a == b; both values print with their type. <type> is bool, char, i8 i16 i32
//                                          i64, u8 u16 u32 u64, f32, f64 or ptr (as <math.h> names sinf and sin)
//   smith_check_near_f32(a, b, tolerance)  |a - b| <= tolerance; a NaN fails; equal infinities pass. Also _f64
//   smith_check_same_bits_f32(a, b)        a and b have the same bits: -0 is not 0, and a NaN is itself. Also _f64
//   SMITH_CHECK_ABORTS(statement)          see below
//
// The SMITH_CHECK macros that follow are the old spelling. They keep working, and print what they always printed (the
// check's file, line, source text and values) when the runner is run by hand, but they are deprecated: new tests call
// the functions. (No compiler warning marks them, since they are used about 3,500 times under -Werror.)
//
// SMITH_CHECK_ABORTS stays a macro, because it is control flow, not text. The statement must run under a sigsetjmp that
// is still on the stack when the statement aborts, and C has no closure to hand it to a function: the function form
// would need a body function and a context struct at every use, more noise than the macro hides. (`.smith`, which has
// closures, can make it a function.) It no longer needs to capture the text: the report reads the line from the stack.
//
//   SMITH_CHECK(condition)            a bool, or anything that converts to one
//   SMITH_CHECK(a, op, b)             op is ==, !=, <, <=, > or >=; each argument is evaluated once, and both values
//   print
//                                     with their types. b converts to a's type as an assignment would, so a lossy
//                                     comparison is a -Wconversion error. a's type is its type as a value: an array
//                                     becomes a pointer and const drops. Pointers compare, and print, as addresses.
//   SMITH_CHECK_NEAR(a, b, tolerance) |a - b| <= tolerance, for float and double. A NaN fails; equal infinities pass.
//   SMITH_CHECK_SAME_BITS(a, b)       a and b are float or double (b converts to a's type) and bitwise identical: -0 is
//                                     not 0, and a NaN is itself. A failure prints both values, shortest and in hex; a
//                                     NaN or an infinity also shows its bits. For determinism: the same inputs must
//                                     give the same bits, not merely near ones.
//   SMITH_CHECK_ABORTS(expression)    the expression stops the program on purpose: it raises SIGABRT or SIGTRAP (an
//                                     assert, abort(), __builtin_trap, the arena's capacity stop) with no sanitizer
//                                     report in its output. In a worker it runs in this process: the signal is caught
//                                     and the check goes on, and the expression's effects (its writes) stay. With
//                                     --isolate it runs in a child process. It fails, showing the expression's output,
//                                     if it returns, exits (which ends the worker), crashes with another signal
//                                     (SIGSEGV), or prints a sanitizer report (which retires the worker): a memory bug
//                                     is not the stop the test expected. A variable the expression changes and the
//                                     test reads afterwards should be volatile, as with any longjmp.
//
// A failed check ends the test, so nothing after it runs. Because the arguments of SMITH_CHECK are separated by
// commas, a comma outside parentheses in an argument (a compound literal's braces) splits it: parenthesize it.
#pragma once

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

#include "arena.h"
#include "random.h"
#include "trace.h"

// A test. The generated tests_list.c holds one row per test in the build's test files: `plain` for
// `test_<name>(void)`, `property` for `test_<name>(SmithKey key)`.
typedef struct {
    const char *name; // without the test_ prefix
    void (*plain)(void);
    void (*property)(SmithKey key);
} SmithTest;

// The table, generated by smith_tests, ended by a row of nullptrs that is not counted.
extern const SmithTest smith_test_table[];
extern const size_t smith_test_count;

// Printed under a failure, whether it is a failed check, a crash or a timeout. Each note replaces the last, and the
// runner clears it before each test and each key. For where the test is, such as a loop's index. It holds 4 KiB; a
// longer note is cut and ends with `…` so the cut shows.
[[gnu::format(printf, 1, 2)]] void smith_test_note(const char *format, ...);

// This test's memory, capacity bytes, from a block the runner reserved once. It is given back before each test and each
// key, so a test never frees. Running out stops the test with a message.
[[nodiscard]] SmithArena smith_test_arena(size_t capacity);

// Whether this run asked for the heavy checks at full size (--full). False by default.
[[nodiscard]] bool smith_test_full(void);

// The plain-function checks. A failed one ends the test, like the macros.
void smith_check(bool condition);

void smith_check_equal_bool(bool a, bool b);
void smith_check_equal_char(char a, char b);
void smith_check_equal_i8(int8_t a, int8_t b);
void smith_check_equal_i16(int16_t a, int16_t b);
void smith_check_equal_i32(int32_t a, int32_t b);
void smith_check_equal_i64(int64_t a, int64_t b);
void smith_check_equal_u8(uint8_t a, uint8_t b);
void smith_check_equal_u16(uint16_t a, uint16_t b);
void smith_check_equal_u32(uint32_t a, uint32_t b);
void smith_check_equal_u64(uint64_t a, uint64_t b);
void smith_check_equal_f32(float a, float b);
void smith_check_equal_f64(double a, double b);
void smith_check_equal_ptr(const volatile void *a, const volatile void *b);
void smith_check_near_f32(float a, float b, float tolerance);
void smith_check_near_f64(double a, double b, double tolerance);
void smith_check_same_bits_f32(float a, float b);
void smith_check_same_bits_f64(double a, double b);

#define SMITH_CHECK(...) \
    SMITH_TEST_SELECT(__VA_ARGS__, SMITH_TEST_COMPARE, SMITH_TEST_TWO_ARGUMENTS, SMITH_TEST_CONDITION, ~)(__VA_ARGS__)
#define SMITH_TEST_SELECT(first, second, third, macro, ...) macro

#define SMITH_TEST_TWO_ARGUMENTS(a, b) \
    do { \
        static_assert(false, "SMITH_CHECK takes a condition, or a, op and b"); \
    } while (0)

#define SMITH_TEST_CONDITION(condition) \
    do { \
        bool smith_check_condition = (condition); \
        if (!smith_check_condition) smith_test_fail_condition(__FILE__, __LINE__); \
    } while (0)

// The comparison is made on the two values by a function that reads the operator's text, so no binary operator is
// written in the macro: a check such as an unsigned >= 0 draws no tautology warning, a float compared with == draws
// no -Wfloat-equal (the test means it), and clangd has no operator to offer to swap inside the call.
#define SMITH_TEST_COMPARE(a, op, b) \
    do { \
        SMITH_TEST_TYPE(a) smith_check_a = (a); \
        SMITH_TEST_TYPE(a) smith_check_b = (b); \
        SmithTestValue smith_check_value_a = SMITH_TEST_VALUE(smith_check_a); \
        SmithTestValue smith_check_value_b = SMITH_TEST_VALUE(smith_check_b); \
        if (!smith_test_compare(smith_check_value_a, #op, smith_check_value_b)) \
            smith_test_fail_compare(#a, #b, smith_check_value_a, smith_check_value_b, __FILE__, __LINE__); \
    } while (0)

#define SMITH_CHECK_NEAR(a, b, tolerance) \
    do { \
        SMITH_TEST_TYPE(a) smith_check_a = (a); \
        SMITH_TEST_TYPE(a) smith_check_b = (b); \
        SMITH_TEST_TYPE(a) smith_check_tolerance = (tolerance); \
        SMITH_TEST_TYPE(a) smith_check_distance = SMITH_TEST_DISTANCE(smith_check_a, smith_check_b); \
        if (!smith_test_within((double)smith_check_distance, (double)smith_check_tolerance)) \
            smith_test_fail_near(#a, #b, #tolerance, \
                                 (SmithTestValue[]){SMITH_TEST_VALUE(smith_check_a), SMITH_TEST_VALUE(smith_check_b), \
                                                    SMITH_TEST_VALUE(smith_check_tolerance), \
                                                    SMITH_TEST_VALUE(smith_check_distance)}, \
                                 __FILE__, __LINE__); \
    } while (0)

// The values are compared by their bits, read by smith_test_same_bits; _Generic has no default, so an integer is a
// compile error.
#define SMITH_CHECK_SAME_BITS(a, b) \
    do { \
        SMITH_TEST_REAL_TYPE(a) smith_check_a = (a); \
        SMITH_TEST_REAL_TYPE(a) smith_check_b = (b); \
        SmithTestValue smith_check_value_a = SMITH_TEST_VALUE(smith_check_a); \
        SmithTestValue smith_check_value_b = SMITH_TEST_VALUE(smith_check_b); \
        if (!smith_test_same_bits(smith_check_value_a, smith_check_value_b)) \
            smith_test_fail_same_bits(#a, #b, smith_check_value_a, smith_check_value_b, __FILE__, __LINE__); \
    } while (0)

#define SMITH_TEST_REAL_TYPE(value) typeof(_Generic((value), float: 0.0F, double: 0.0))

// The sigsetjmp is here, not in a function, because it must return into a frame that is still live: the runner jumps
// back to it when the expression raises SIGABRT or SIGTRAP, or a check inside it fails.
#define SMITH_CHECK_ABORTS(expression) \
    do { \
        if (sigsetjmp(*smith_test_abort_jump_buffer(), 1) == 0) { \
            if (smith_test_abort_child(__FILE__, __LINE__)) { \
                (void)(expression); \
                smith_test_abort_returned(); \
            } \
        } \
        smith_test_abort_judge(__FILE__, __LINE__); \
    } while (0)

// The type of value as an expression's result: an array becomes a pointer, const drops, and no integer promotion
// applies. Every pointer's type is const volatile void *, so any two pointers compare, as addresses. The types are the
// ones SMITH_TEST_VALUE prints. (Written with _Generic, not with a test on the type's class: clangd offers to swap the
// operands of an == in a macro, and fails.)
#define SMITH_TEST_TYPE(value) \
    typeof(_Generic((value), \
               bool: false, \
               char: (char)0, \
               signed char: (signed char)0, \
               unsigned char: (unsigned char)0, \
               short: (short)0, \
               unsigned short: (unsigned short)0, \
               int: 0, \
               unsigned int: 0U, \
               long: 0L, \
               unsigned long: 0UL, \
               long long: 0LL, \
               unsigned long long: 0ULL, \
               float: 0.0F, \
               double: 0.0, \
               default: (const volatile void *)nullptr))

#define SMITH_TEST_DISTANCE(a, b) \
    _Generic((a), float: smith_test_distance_float, double: smith_test_distance_double)(a, b)

// What a value looks like when printed: its kind, the type as written in C, and the value.
typedef enum : unsigned char {
    SmithTestKind_Unsigned,
    SmithTestKind_Signed,
    SmithTestKind_Float,
    SmithTestKind_Double,
    SmithTestKind_Bool,
    SmithTestKind_Char,
    SmithTestKind_Pointer,
} SmithTestKind;

typedef struct {
    SmithTestKind kind;
    const char *type; // "int", "unsigned long", "double", "pointer"
    union {
        unsigned long long unsigned_value;
        long long signed_value;
        float float_value;
        double double_value;
        bool bool_value;
        char char_value;
        uintptr_t pointer_value;
    };
} SmithTestValue;

#define SMITH_TEST_VALUE(value) \
    _Generic((value), \
        bool: smith_test_value_bool, \
        char: smith_test_value_char, \
        signed char: smith_test_value_signed_char, \
        unsigned char: smith_test_value_unsigned_char, \
        short: smith_test_value_short, \
        unsigned short: smith_test_value_unsigned_short, \
        int: smith_test_value_int, \
        unsigned int: smith_test_value_unsigned_int, \
        long: smith_test_value_long, \
        unsigned long: smith_test_value_unsigned_long, \
        long long: smith_test_value_long_long, \
        unsigned long long: smith_test_value_unsigned_long_long, \
        float: smith_test_value_float, \
        double: smith_test_value_double, \
        default: smith_test_value_pointer)(value)

[[nodiscard]] SmithTestValue smith_test_value_bool(bool value);
[[nodiscard]] SmithTestValue smith_test_value_char(char value);
[[nodiscard]] SmithTestValue smith_test_value_signed_char(signed char value);
[[nodiscard]] SmithTestValue smith_test_value_unsigned_char(unsigned char value);
[[nodiscard]] SmithTestValue smith_test_value_short(short value);
[[nodiscard]] SmithTestValue smith_test_value_unsigned_short(unsigned short value);
[[nodiscard]] SmithTestValue smith_test_value_int(int value);
[[nodiscard]] SmithTestValue smith_test_value_unsigned_int(unsigned int value);
[[nodiscard]] SmithTestValue smith_test_value_long(long value);
[[nodiscard]] SmithTestValue smith_test_value_unsigned_long(unsigned long value);
[[nodiscard]] SmithTestValue smith_test_value_long_long(long long value);
[[nodiscard]] SmithTestValue smith_test_value_unsigned_long_long(unsigned long long value);
[[nodiscard]] SmithTestValue smith_test_value_float(float value);
[[nodiscard]] SmithTestValue smith_test_value_double(double value);
[[nodiscard]] SmithTestValue smith_test_value_pointer(const volatile void *value);

// Whether a op b holds, for the operator written as text: "==", "!=", "<", "<=", ">" or ">=". The values have one
// type. A NaN is unordered, so every operator but != fails. Any other text stops the test.
[[nodiscard]] bool smith_test_compare(SmithTestValue a, const char *op, SmithTestValue b);
// Whether a and b, floats of one kind, have the same bits.
[[nodiscard]] bool smith_test_same_bits(SmithTestValue a, SmithTestValue b);
// Whether distance <= tolerance; a NaN in either fails.
[[nodiscard]] bool smith_test_within(double distance, double tolerance);
[[nodiscard]] float smith_test_distance_float(float a, float b);
[[nodiscard]] double smith_test_distance_double(double a, double b);

// The macros' failures. Each records where the check is and what the values were, for the runner to print, and ends
// the process.
[[noreturn]] void smith_test_fail_condition(const char *file, int line);
[[noreturn]] void smith_test_fail_compare(const char *a_text, const char *b_text, SmithTestValue a, SmithTestValue b,
                                          const char *file, int line);
[[noreturn]] void smith_test_fail_same_bits(const char *a_text, const char *b_text, SmithTestValue a, SmithTestValue b,
                                            const char *file, int line);
// values holds a, b, the tolerance and their distance.
[[noreturn]] void smith_test_fail_near(const char *a_text, const char *b_text, const char *tolerance_text,
                                       const SmithTestValue values[4], const char *file, int line);

// SMITH_CHECK_ABORTS. In a worker, abort_child captures the output and returns true; the expression runs;
// abort_returned notes that it returned; and abort_judge fails the check unless the expression stopped by SIGABRT or
// SIGTRAP with no sanitizer report in its output. A SIGABRT or SIGTRAP, or a failed check inside the expression, jumps
// back to the macro's sigsetjmp (abort_jump_buffer), which goes straight to abort_judge. With --isolate the child
// forks: it returns true in the child, which runs the expression and exits in abort_returned, and false in the parent,
// whose abort_judge reads how the child ended.
[[nodiscard]] sigjmp_buf *smith_test_abort_jump_buffer(void);
[[nodiscard]] bool smith_test_abort_child(const char *file, int line);
void smith_test_abort_returned(void);
void smith_test_abort_judge(const char *file, int line);
