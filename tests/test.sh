#!/bin/sh
# Tests of <smith/test.h>, smith_tests and the runner, with real clang, ninja and clangd. Usage: sh tests/test.sh path/to/smith
# A scratch project builds a runner from test files that fail on purpose, and each case runs it and compares the output.
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
repo=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
failures=0
total=0
out=
err=
rc=0

capture() {
    out=$("$@" 2>"$work/err")
    rc=$?
    err=$(cat "$work/err")
}

expect() { # name, expected, actual
    total=$((total + 1))
    if [ "$2" = "$3" ]; then
        echo "ok    $1"
    else
        failures=$((failures + 1))
        echo "FAIL  $1"
        printf '  expected: %s\n' "$2"
        printf '  actual:   %s\n' "$3"
        case $1 in *builds*) printf '%s\n%s\n' "$out" "$err" ;; esac
    fi
}

expect_contains() { # name, text, needle
    total=$((total + 1))
    case $2 in
    *"$3"*) echo "ok    $1" ;;
    *)
        failures=$((failures + 1))
        echo "FAIL  $1"
        echo "  expected to contain: $3"
        echo "  actual:              $2"
        ;;
    esac
}

expect_lacks() { # name, text, needle
    total=$((total + 1))
    case $2 in
    *"$3"*)
        failures=$((failures + 1))
        echo "FAIL  $1"
        echo "  expected not to contain: $3"
        echo "  actual:                  $2"
        ;;
    *) echo "ok    $1" ;;
    esac
}

mkdir "$work/bin" && ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith++"
PATH="$work/bin:$PATH"

# A project: the build.c of a project with smith_tests, with the sanitizers on unless -DSAN=0, and its test files.
# Usage: project <folder>; then the test files are written to <folder>/tests.
project() {
    mkdir -p "$1/tests" && cd "$1" || exit 1
    cat >build.c <<'C'
#include <smith/build.h>

#include <string.h>

void build(SmithBuild *b) {
    bool sanitize = strcmp(smith_option_value(b, "SAN", "1", "0 builds without the sanitizers"), "1") == 0;
    const char *const *flags = smith_cat(b, smith_list(b, "-std=gnu23", "-g", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-I", ".smith/include", nullptr),
                                         smith_when(b, sanitize, smith_list(b, "-fsanitize=address,undefined", nullptr)), nullptr);
    SmithObject arena = {.compiler = "smith", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
    smith_object(b, &arena);
    SmithObject random = {.compiler = "smith", .source = smith_library(b, "random"), .output = "build/smith/random.o", .flags = flags};
    smith_object(b, &random);
    SmithTests tests = {
        .sources = smith_glob(b, smith_list(b, "tests/test_*.c", nullptr)),
        .objects = (const SmithObject *[]){&arena, &random, nullptr},
        .flags = flags,
        .link_flags = smith_when(b, sanitize, smith_list(b, "-fsanitize=address,undefined", nullptr)),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
    smith_compile_commands(b, "compile_commands.json");
}
C
}

# The runner's output without the timings: the slowest list is dropped and the summary's seconds are masked.
plain() {
    sed -e '/^slowest:/,/^$/d' -e 's/ in [0-9.]* s$//' | sed '/./,$!d'
}

run_plain() { # runner arguments
    "$work/main/build/tests" "$@" 2>&1 | plain
}

line_of() { # file, text: the line number of the first line holding the text
    grep -n -F "$2" "$1" | head -n 1 | cut -d: -f1
}

# 1. The checks: a project with passing and failing tests.
project "$work/main"
cat >tests/test_checks.c <<'C'
#include <smith/test.h>

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

void test_passing(void) {
    printf("this output is hidden\n");
    SMITH_CHECK(1 + 1, ==, 2);
    SMITH_CHECK(true);
    SMITH_CHECK_NEAR(0.1 + 0.2, 0.3, 1e-9);
    SMITH_CHECK_NEAR(1.0f, 1.0001f, 0.001f);
    int array[3] = {1, 2, 3};
    SMITH_CHECK(array, ==, &array[0]);
    SMITH_CHECK_ABORTS(assert(false));
}

void test_compare(void) {
    long long x = 7;
    struct {
        long long high;
    } range = {6};
    printf("the test's output\n");
    smith_test_note("loop %d", 3);
    SMITH_CHECK(x, <=, range.high);
}

void test_condition(void) {
    int items = 0;
    SMITH_CHECK(items > 0);
}

void test_unsigned(void) {
    unsigned int small = 3;
    SMITH_CHECK(small, >=, 4);
}

void test_float_bits(void) {
    SMITH_CHECK(0.1 + 0.2, ==, 0.3);
}

void test_char(void) {
    char c = 'a';
    SMITH_CHECK(c, ==, 'b');
}

void test_bool(void) {
    bool flag = true;
    SMITH_CHECK(flag, ==, false);
}

void test_pointer(void) {
    int value = 0;
    int *pointer = &value;
    SMITH_CHECK(pointer, ==, nullptr);
}

void test_near(void) {
    SMITH_CHECK_NEAR(1.0, 1.5, 0.1);
}

void test_near_nan(void) {
    SMITH_CHECK_NEAR(__builtin_nan(""), 1.0, 1e300);
}

void test_stops(void) {
    SMITH_CHECK(1, ==, 2);
    printf("never printed\n");
}
C
capture "$smith" build
expect "build: the runner builds" 0 "$rc"
expect "build: the scan writes the prototypes" yes "$([ -f build/tests_list.h ] && echo yes)"
expect "build: ... and the table" yes "$([ -f build/tests_list.c ] && echo yes)"
expect_contains "build: the table lists a test by its name" "$(cat build/tests_list.c)" '{"compare", test_compare, nullptr},'
capture build/tests --list
expect "list: names, in file order" "passing
compare
condition
unsigned
float_bits
char
bool
pointer
near
near_nan
stops" "$out"

expect "pass: the line and the summary, nothing else" "PASS passing

1 passed, 0 failed" "$(run_plain --test passing --verbose)"
expect "pass: without --verbose only the summary" "1 passed, 0 failed" "$(run_plain --test passing)"

line=$(line_of tests/test_checks.c 'SMITH_CHECK(x, <=, range.high);')
expect "compare: the failure block" "FAIL compare
  tests/test_checks.c:$line
      SMITH_CHECK(x, <=, range.high);
    x          = 7   (long long)
    range.high = 6   (long long)
    note: loop 3
    the test's output

0 passed, 1 failed" "$(run_plain --test compare)"

line=$(line_of tests/test_checks.c 'SMITH_CHECK(items > 0);')
expect "condition: the failure block" "FAIL condition
  tests/test_checks.c:$line
      SMITH_CHECK(items > 0);
    the condition is false

0 passed, 1 failed" "$(run_plain --test condition)"

line=$(line_of tests/test_checks.c 'SMITH_CHECK(small, >=, 4);')
expect "types: an unsigned int prints as one, and a literal has no =" "FAIL unsigned
  tests/test_checks.c:$line
      SMITH_CHECK(small, >=, 4);
    small = 3   (unsigned int)
    4           (unsigned int)

0 passed, 1 failed" "$(run_plain --test unsigned)"

line=$(line_of tests/test_checks.c 'SMITH_CHECK(0.1 + 0.2, ==, 0.3);')
expect "types: a double prints its shortest text and its hex, so the last bit shows" "FAIL float_bits
  tests/test_checks.c:$line
      SMITH_CHECK(0.1 + 0.2, ==, 0.3);
    0.1 + 0.2 = 0.30000000000000004 [0x1.3333333333334p-2]   (double)
    0.3       = 0.3 [0x1.3333333333333p-2]                   (double)

0 passed, 1 failed" "$(run_plain --test float_bits)"

line=$(line_of tests/test_checks.c "SMITH_CHECK(c, ==, 'b');")
expect "types: a char prints as a character" "FAIL char
  tests/test_checks.c:$line
      SMITH_CHECK(c, ==, 'b');
    c = 'a'   (char)
    'b'       (char)

0 passed, 1 failed" "$(run_plain --test char)"

line=$(line_of tests/test_checks.c 'SMITH_CHECK(flag, ==, false);')
expect "types: a bool prints true and false" "FAIL bool
  tests/test_checks.c:$line
      SMITH_CHECK(flag, ==, false);
    flag = true    (bool)
    false          (bool)

0 passed, 1 failed" "$(run_plain --test bool)"

capture build/tests --test pointer
expect_contains "types: a pointer prints as an address" "$out" "    pointer = 0x"
expect_contains "types: ... and nullptr as nullptr" "$out" "    nullptr "

line=$(line_of tests/test_checks.c 'SMITH_CHECK_NEAR(1.0, 1.5, 0.1);')
expect "near: the failure block has the distance" "FAIL near
  tests/test_checks.c:$line
      SMITH_CHECK_NEAR(1.0, 1.5, 0.1);
    1.0      = 1 [0x1p+0]                   (double)
    1.5      = 1.5 [0x1.8p+0]               (double)
    0.1      = 0.1 [0x1.999999999999ap-4]   (double)
    distance = 0.5 [0x1p-1]                 (double)

0 passed, 1 failed" "$(run_plain --test near)"

capture build/tests --test near_nan
expect_contains "near: a NaN fails" "$out" "FAIL near_nan"
expect_contains "near: ... and prints the NaN" "$out" "= nan"

capture build/tests --test stops
expect_lacks "stop: a failed check ends the test" "$out" "never printed"

# 2. Aborts.
cat >tests/test_aborts.c <<'C'
#include <smith/test.h>

#include <assert.h>
#include <signal.h>
#include <stdio.h>

void test_abort_assert(void) {
    SMITH_CHECK_ABORTS((printf("child noise\n"), assert(1 > 2)));
}

void test_abort_arena(void) {
    SmithArena arena = smith_test_arena(128);
    SMITH_CHECK_ABORTS((void)smith_arena_push(&arena, 1000, 8));
}

void test_abort_signal(void) {
    SMITH_CHECK_ABORTS(raise(SIGSEGV));
}

void test_abort_returns(void) {
    SMITH_CHECK_ABORTS(printf("child output\n"));
}

void test_abort_check_inside(void) {
    SMITH_CHECK_ABORTS(smith_test_fail_condition(__FILE__, __LINE__));
}
C
capture "$smith" build
expect "aborts: the runner builds" 0 "$rc"
capture build/tests --test abort_assert --verbose
expect_contains "aborts: an assert passes" "$out" "PASS abort_assert"
expect_lacks "aborts: ... and the child's output is hidden" "$out" "child noise"
capture build/tests --test abort_arena --verbose
expect_contains "aborts: the arena's capacity stop passes" "$out" "PASS abort_arena"
capture build/tests --test abort_signal --verbose
expect_contains "aborts: a signal passes" "$out" "PASS abort_signal"
line=$(line_of tests/test_aborts.c 'SMITH_CHECK_ABORTS(printf("child output\n"));')
expect "aborts: returning fails, and shows the child's output" "FAIL abort_returns
  tests/test_aborts.c:$line
      SMITH_CHECK_ABORTS(printf(\"child output\\n\"));
    expected an abort; it returned
    child output

0 passed, 1 failed" "$(run_plain --test abort_returns)"
capture build/tests --test abort_check_inside
expect_contains "aborts: a check inside the expression is not an abort" "$out" "expected an abort; a check inside it failed"

# 3. Isolation: a crash, a sanitizer report and a timeout each fail one test and the rest still run.
cat >tests/test_isolation.c <<'C'
#include <smith/test.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void test_overflow(void) {
    char *buffer = malloc(8);
    smith_test_note("index %d", 8);
    buffer[8] = 'x';
    free(buffer);
}

void test_sleeps(void) {
    sleep(30);
}

void test_exits(void) {
    exit(3);
}

void test_alarm_hangs(void) {
    alarm(1);
    sleep(30);
}

void test_long_note(void) {
    char text[6000];
    memset(text, 'a', sizeof text - 1);
    text[sizeof text - 1] = '\0';
    smith_test_note("%s", text);
    SMITH_CHECK(1, ==, 2);
}

void test_board_note(void) {
    smith_test_note("board\n  . x .\n  x . x\n  . x .\nend");
    SMITH_CHECK(1, ==, 2);
}

void test_ok_after(void) {
    SMITH_CHECK(1, ==, 1);
}
C
capture "$smith" build
expect "isolation: the runner builds" 0 "$rc"
capture build/tests --test overflow
expect_contains "isolation: an ASan report is shown" "$out" "AddressSanitizer: heap-buffer-overflow"
expect_contains "isolation: ... under the test's FAIL line" "$out" "FAIL overflow: killed by SIGABRT"
expect_contains "isolation: ... with the note" "$out" "    note: index 8"
capture build/tests --test sleeps --timeout 1
expect_contains "isolation: a timeout is reported" "$out" "FAIL sleeps: timed out after 1.0 s"
capture build/tests --test alarm_hangs
expect_contains "isolation: a test's own alarm ends it as a timeout, with the measured time" "$out" "FAIL alarm_hangs: timed out after 1.0 s"
capture build/tests --test board_note
expect_contains "note: several lines show in full" "$out" "    note: board
  . x .
  x . x
  . x .
end"
expect_lacks "note: ... without the mark" "$out" "…"
capture build/tests --test long_note
expect_contains "note: an overlong one ends with the mark" "$out" "aaa…"
expect_lacks "note: ... and holds no more than 4 KiB" "$out" "$(printf 'a%.0s' $(seq 1 4100))"
capture build/tests --test exits
expect_contains "isolation: an exit status is reported" "$out" "FAIL exits: exited with status 3"
capture build/tests --test ok_after --verbose
expect_contains "isolation: a test after the others still runs" "$out" "PASS ok_after"
capture build/tests --timeout 1
expect_contains "isolation: the whole run reports every test" "$out" "failed in"
expect "isolation: ... and exits 1" 1 "$rc"
capture build/tests --jobs 0
expect "options: a bad option exits 2" 2 "$rc"
expect_contains "options: ... and prints the usage" "$err" "usage:"
capture build/tests --test nothing_like_this
expect "options: no matching test exits 1" "1 no test matches \"nothing_like_this\"" "$rc $out"

# 4. Running: -DTEST filters, --seeds counts, --jobs 1 gives the same results.
cat >tests/test_properties.c <<'C'
#include <smith/test.h>

void test_prop_ok(SmithKey key) {
    long long x = smith_uniform_int64_sample(key, (SmithUniformInt64){.low = 0, .high = 1000});
    SMITH_CHECK(x, <=, 1000);
}

void test_prop_fails(SmithKey key) {
    long long x = smith_uniform_int64_sample(key, (SmithUniformInt64){.low = 0, .high = 1000});
    SMITH_CHECK(x, <, 500);
}
C
capture "$smith" build
expect "properties: the runner builds" 0 "$rc"
capture build/tests --test prop_ok --verbose
expect_contains "properties: a property test runs on 100 keys" "$out" "PASS prop_ok (100 seeds)"
capture build/tests --test prop_ok --seeds 7 --verbose
expect_contains "seeds: --seeds sets the count" "$out" "PASS prop_ok (7 seeds)"
capture build/tests --test prop_ok --seed 5 --verbose
expect_contains "seeds: --seed runs one seed" "$out" "PASS prop_ok (seed 5)"
capture "$smith" build test -DTEST=prop_ok -DSEEDS=3
expect "task: smith build test runs the runner" 0 "$rc"
capture "$smith" build test -DTEST=prop_ok -DSEEDS=x
expect "task: a bad option reaches the runner's usage, and fails the task" yes "$([ "$rc" -ne 0 ] && echo yes)"
expect_contains "task: ... which says so" "$err$out" "usage:"
capture "$smith" build test -DTEST=ok_after -DJOBS=1
expect "task: -DTEST filters, and a passing run exits 0" 0 "$rc"
capture "$smith" build test -DTEST=stops
expect_contains "task: a failing test is printed" "$out$err" "FAIL stops"
expect "task: ... and the task fails" 1 "$rc"
all=$(build/tests --test abort 2>&1 | plain)
one=$(build/tests --test abort --jobs 1 2>&1 | plain)
expect "jobs: --jobs 1 gives the same results" "$all" "$one"

capture build/tests --test prop_fails --shrink 0
seed=$(printf '%s' "$out" | sed -n 's/^FAIL prop_fails with seed \([0-9]*\).*/\1/p')
expect "properties: a failure names its seed" yes "$([ -n "$seed" ] && echo yes)"
expect_contains "properties: ... and a rerun line" "$out" "    rerun: smith build test -DTEST=prop_fails -DSEED=$seed"
first=$(printf '%s' "$out" | sed -n '/^FAIL prop_fails/,/rerun/p')
capture "$smith" build test -DTEST=prop_fails -DSEED="$seed" -DSHRINK=0
second=$(printf '%s%s' "$out" "$err" | sed -n '/^FAIL prop_fails/,/rerun/p')
expect "properties: the rerun line reproduces the failure" "$first" "$second"
capture build/tests --test prop_fails --seed "$seed" --shrink 0
third=$(printf '%s' "$out" | sed -n '/^FAIL prop_fails/,/rerun/p')
expect "properties: --seed reproduces it too" "$first" "$third"

# 4b. Shrinking.
cat >tests/test_shrink.c <<'C'
#include <smith/test.h>

void test_shrink_int(SmithKey key) {
    long long x = smith_uniform_int64_sample(key, (SmithUniformInt64){.low = 0, .high = 1000});
    SMITH_CHECK(x, <, 500);
}

void test_shrink_list(SmithKey key) {
    long long length = smith_uniform_int64_sample(smith_key_derive(key, 0), (SmithUniformInt64){.low = 0, .high = 8});
    for (long long i = 0; i < length; i++) {
        long long item = smith_uniform_int64_sample(smith_key_derive(key, (uint64_t)i + 1), (SmithUniformInt64){.low = 0, .high = 100});
        smith_test_note("length %lld, item %lld", length, i);
        SMITH_CHECK(item, <=, 90);
    }
}

void test_shrink_normal(SmithKey key) {
    double z = smith_normal_double_sample(key, (SmithNormalDouble){.mean = 0, .deviation = 1});
    SMITH_CHECK(z, <, 3.0);
}

void test_shrink_moves(SmithKey key) {
    long long x = smith_uniform_int64_sample(key, (SmithUniformInt64){.low = 0, .high = 1000});
    SMITH_CHECK(x, <, 500);
    SMITH_CHECK(x, !=, 0); // the failure that shrinking to 0 would move to
}
C
capture "$smith" build
expect "shrink: the runner builds" 0 "$rc"

line=$(line_of tests/test_shrink.c 'SMITH_CHECK(x, <, 500);')
capture build/tests --test shrink_int
seed=$(printf '%s' "$out" | sed -n 's/^FAIL shrink_int with seed \([0-9]*\),.*/\1/p')
expect "shrink: a uniform [0, 1000] with x < 500 shrinks to 500" "FAIL shrink_int with seed $seed, shrunk in RUNS runs
  tests/test_shrink.c:$line
      SMITH_CHECK(x, <, 500);
    x = 500   (long long)
    500       (long long)
    rerun: smith build test -DTEST=shrink_int -DSEED=$seed

0 passed, 1 failed" "$(printf '%s' "$out" | plain | sed 's/shrunk in [0-9]* runs/shrunk in RUNS runs/')"
runs=$(printf '%s' "$out" | sed -n 's/.*shrunk in \([0-9]*\) runs.*/\1/p')
expect "shrink: ... in at most 1000 runs" yes "$([ -n "$runs" ] && [ "$runs" -le 1000 ] && echo yes)"

capture build/tests --test shrink_int --shrink 0
unshrunk=$(printf '%s' "$out" | sed -n 's/^    x = \([0-9]*\) .*/\1/p')
expect "shrink: --shrink 0 shows the unshrunk failure" yes "$([ "$unshrunk" -gt 500 ] && echo yes)"
expect_lacks "shrink: ... without 'shrunk'" "$out" "shrunk"

capture "$smith" build test -DTEST=shrink_int -DSEED="$seed"
expect_contains "shrink: the rerun line shrinks to the same case" "$out$err" "    x = 500   (long long)"

first=$(build/tests --test shrink_list 2>&1 | plain)
expect_contains "shrink: a list shrinks to one item" "$first" "    note: length 1, item 0"
expect_contains "shrink: ... of 91" "$first" "    item = 91   (long long)"
second=$(build/tests --test shrink_list 2>&1 | plain)
expect "shrink: the same seed gives the same shrunk case" "$first" "$second"

capture build/tests --test shrink_normal --seeds 5000
z=$(printf '%s' "$out" | sed -n 's/^    z *= \([0-9.e+-]*\) .*/\1/p')
expect_contains "shrink: a normal with z < 3 shrinks" "$out" "FAIL shrink_normal with seed"
expect "shrink: ... to the z nearest 3 from above" yes "$(awk -v z="$z" 'BEGIN { print (z >= 3 && z < 3.0000001) ? "yes" : "no" }')"

line=$(grep -n -F 'SMITH_CHECK(x, <, 500);' tests/test_shrink.c | sed -n 2p | cut -d: -f1)
capture build/tests --test shrink_moves
expect_contains "shrink: a failure that moves to another check is not kept: x is not 0" "$out" "    x = 500   (long long)"
expect_contains "shrink: ... and it is the first check" "$out" "  tests/test_shrink.c:$line"

# Shrinking does not make another test depend on it: while one test shrinks for seconds, a healthy test that writes more
# than 64 KiB, waits a second and writes more, still passes with --timeout 2.
project "$work/alongside"
cat >tests/test_alongside.c <<'C'
#include <smith/test.h>

#include <stdio.h>

void test_a_shrinks_slowly(SmithKey key) {
    usleep(3000); // each of the 1000 runs of shrinking takes this long: several seconds in all
    long long smallest = 1000000000;
    for (uint64_t i = 0; i < 200; i++) {
        long long v = smith_uniform_int64_sample(smith_key_derive(key, i), (SmithUniformInt64){.low = 0, .high = 1000000000});
        if (v < smallest) smallest = v;
    }
    SMITH_CHECK(smallest, <, 1000);
}

void test_b_writes_a_lot(void) {
    for (int i = 0; i < 1000; i++) printf("%0100d\n", i); // 100 KB, past the 64 KiB the runner keeps
    usleep(1000000);                                      // while the runner shrinks the other test
    for (int i = 0; i < 3000; i++) printf("%0100d\n", i); // 300 KB more: it blocks if nobody reads the pipe
    printf("done\n");
}
C
sed -i.bak 's/#include <stdio.h>/#include <stdio.h>\n#include <unistd.h>/' tests/test_alongside.c && rm tests/test_alongside.c.bak
capture "$smith" build -DSAN=0
expect "alongside: the runner builds" 0 "$rc"
capture build/tests --timeout 2 --jobs 2
expect_contains "alongside: the shrinking test fails and is shrunk" "$out" "FAIL a_shrinks_slowly with seed"
expect_lacks "alongside: ... and the other test is not a failure" "$out" "FAIL b_writes_a_lot"
expect_contains "alongside: ... it passes" "$out" "1 passed, 1 failed"
cd "$work/main" || exit 1

# The budget: a test that needs far more than 1000 runs to shrink stops at 1000 runs or 10 seconds.
project "$work/budget"
cat >tests/test_budget.c <<'C'
#include <smith/test.h>

void test_budget(SmithKey key) {
    long long smallest = 1000000000;
    for (uint64_t i = 0; i < 200; i++) {
        long long v = smith_uniform_int64_sample(smith_key_derive(key, i), (SmithUniformInt64){.low = 0, .high = 1000000000});
        if (v < smallest) smallest = v;
    }
    SMITH_CHECK(smallest, <, 1000);
}
C
capture "$smith" build -DSAN=0
expect "budget: the runner builds" 0 "$rc"
start=$(date +%s)
capture build/tests --test budget
elapsed=$(($(date +%s) - start))
runs=$(printf '%s' "$out" | sed -n 's/.*shrunk in \([0-9]*\) runs.*/\1/p')
expect "budget: it shrinks, and stops within 1000 runs" yes "$([ -n "$runs" ] && [ "$runs" -le 1000 ] && echo yes)"
expect "budget: ... and within about 10 seconds" yes "$([ "$elapsed" -le 14 ] && echo yes)"
cd "$work/main" || exit 1

# 5. SIGSEGV without the sanitizers is named.
project "$work/plain"
cat >tests/test_crash.c <<'C'
#include <smith/test.h>

void test_crash(void) {
    volatile int *pointer = nullptr;
    smith_test_note("about to crash");
    *pointer = 1;
}

void test_survivor(void) {
    SMITH_CHECK(true);
}
C
capture "$smith" build -DSAN=0
expect "crash: the runner builds without sanitizers" 0 "$rc"
capture build/tests
expect_contains "crash: a SIGSEGV is named" "$out" "FAIL crash: killed by SIGSEGV"
expect_contains "crash: ... with the note" "$out" "    note: about to crash"
expect_contains "crash: ... and the other test passed" "$out" "1 passed, 1 failed"

# 6. Finding the tests.
project "$work/finding"
cat >tests/test_a.c <<'C'
#include <smith/test.h>

void test_same(void) {
}
C
cat >tests/test_b.c <<'C'
#include <smith/test.h>

void test_same(void) {
}
C
capture "$smith" build
expect "duplicate: a name defined twice fails the build" 1 "$rc"
expect_contains "duplicate: ... naming both files" "$out$err" "smith: smith_tests: test_same is defined in tests/test_a.c and in tests/test_b.c"
rm tests/test_b.c
cat >tests/test_b.c <<'C'
#include <smith/test.h>

void test_odd (void) {
}
C
capture "$smith" build
expect "formatting: a test the scan missed fails to compile" 1 "$rc"
expect_contains "formatting: ... with the missing prototype" "$out$err" "no previous prototype for function 'test_odd'"
cat >tests/test_b.c <<'C'
#include <smith/test.h>

[[maybe_unused]] static int helper(int value) {
    return value + 1;
}

[[maybe_unused]] static int not_a_test(int value) {
    return helper(value);
}
C
capture "$smith" build
expect "no tests: a file without tests is fine" 0 "$rc"
capture build/tests --list
expect "no tests: ... and adds nothing" "same" "$out"
rm tests/test_a.c
capture "$smith" build
expect "no tests: a project with none builds" 0 "$rc"
capture build/tests
expect_contains "no tests: ... and runs nothing" "$out" "0 passed, 0 failed"
expect "no tests: ... with status 0" 0 "$rc"

# 7. Conversions: a lossy comparison is a -Wconversion error.
project "$work/lossy"
cat >tests/test_lossy.c <<'C'
#include <smith/test.h>

void test_lossy(void) {
    int count = 3;
    SMITH_CHECK(count, ==, 3.5);
}
C
capture "$smith" build
expect "conversion: a lossy comparison fails to compile" 1 "$rc"
expect_contains "conversion: ... with -Wconversion's message" "$out$err" "implicit conversion from 'double' to"

# 8. smith_tests refuses objects without the arena and random.
project "$work/missing"
cat >tests/test_x.c <<'C'
#include <smith/test.h>

void test_x(void) {
}
C
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu23", "-I", ".smith/include", nullptr);
    SmithObject arena = {.compiler = "smith", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
    smith_object(b, &arena);
    SmithTests tests = {
        .sources = smith_list(b, "tests/test_x.c", nullptr),
        .objects = (const SmithObject *[]){&arena, nullptr},
        .flags = flags,
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
C
capture "$smith" build
expect "objects: a missing library is an error" 2 "$rc"
expect_contains "objects: ... that says which library to add" "$err" "smith: build.c: smith_tests: .objects must include smith's random, which the tests link once; add a SmithObject compiled from smith_library(b, \"random\") to .objects"

# 9. Tools: clangd reads a test file without errors.
cd "$work/main" || exit 1
if [ -x /usr/bin/clangd ]; then
    capture "$smith" build
    capture /usr/bin/clangd --check=tests/test_checks.c
    errors=$(printf '%s\n%s\n' "$out" "$err" | grep -c 'E\[')
    expect "clangd: no errors in a test file" 0 "$errors"
fi

# 10. The libraries compile clean under the flags `smith new` writes, in both C standards.
flags=$(awk '/build_warnings\[\] =/ {on = 1} on {print} /nullptr};/ {on = 0}' "$repo/templates/new/build.c" | grep -o '"-[^"]*"' | tr -d '"' | grep -v '^-std=')
for standard in gnu2y gnu23; do
    for library in test random arena; do
        # shellcheck disable=SC2086
        capture /usr/bin/clang -std=$standard $flags -I "$repo/include" -c "$repo/lib/$library.c" -o "$work/$library.o"
        expect "flags: lib/$library.c compiles clean under $standard with the template's warnings" 0 "$rc"
        [ "$rc" -eq 0 ] || printf '%s\n%s\n' "$out" "$err"
    done
done

echo
echo "$total test checks, $failures failed"
[ "$failures" -eq 0 ]
