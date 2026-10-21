#!/bin/sh
# Tests of <smith/test.h>, smith_tests and the runner, with real clang, ninja and clangd.
# Usage: sh tests/test.sh path/to/smith [isolate]
# The runner's workers are the default; with "isolate" every runner is run with --isolate (`smith test --isolate`), the
# process per test, and every check must give the same result.
# A scratch project builds a runner from test files that fail on purpose, and each case runs it and compares the output.
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
iso=
if [ "${2:-}" = isolate ]; then
    iso=--isolate
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
export XDG_CACHE_HOME=$work/xdg-cache # the module and package caches of the tests, never the real ones
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

mkdir "$work/bin" && ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" && ln -s "$smith" "$work/bin/smith-c++"
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
                                         smith_when(b, sanitize, smith_list(b, "-fsanitize=address,undefined", "-fno-sanitize-recover=all", nullptr)), nullptr);
    SmithObject arena = {.compiler = "smith-cc", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
    smith_object(b, &arena);
    SmithObject random = {.compiler = "smith-cc", .source = smith_library(b, "random"), .output = "build/smith/random.o", .flags = flags};
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
    "$work/main/build/tests" $iso "$@" 2>&1 | plain
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
capture build/tests $iso --list
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

capture build/tests $iso --test pointer
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

capture build/tests $iso --test near_nan
expect_contains "near: a NaN fails" "$out" "FAIL near_nan"
expect_contains "near: ... and prints the NaN" "$out" "= nan"

capture build/tests $iso --test stops
expect_lacks "stop: a failed check ends the test" "$out" "never printed"

# 2. Aborts.
cat >tests/test_aborts.c <<'C'
#include <smith/test.h>

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

void test_abort_assert(void) {
    SMITH_CHECK_ABORTS((printf("child noise\n"), assert(1 > 2)));
}

void test_abort_arena(void) {
    SmithArena arena = smith_test_arena(128);
    SMITH_CHECK_ABORTS((void)smith_arena_push(&arena, 1000, 8));
}

void test_abort_abort(void) {
    SMITH_CHECK_ABORTS(abort());
}

void test_abort_trap(void) {
    SMITH_CHECK_ABORTS(__builtin_trap());
}

void test_sanitized_read(void) {
    char *bytes = malloc(8);
    volatile size_t index = 8;
    SMITH_CHECK_ABORTS((printf("before\n"), (void)bytes[index]));
    free(bytes);
}

void test_sanitized_overflow(void) {
    volatile int largest = INT_MAX;
    SMITH_CHECK_ABORTS((void)(largest + 1));
}

void test_abort_exit(void) {
    SMITH_CHECK_ABORTS(exit(3));
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
capture build/tests $iso --test abort_assert --verbose
expect_contains "aborts: an assert passes" "$out" "PASS abort_assert"
expect_lacks "aborts: ... and the child's output is hidden" "$out" "child noise"
capture build/tests $iso --test abort_arena --verbose
expect_contains "aborts: the arena's capacity stop passes" "$out" "PASS abort_arena"
for name in abort_abort abort_trap; do
    capture build/tests $iso --test $name --verbose
    expect_contains "aborts: $name passes" "$out" "PASS $name"
done
capture build/tests $iso --test sanitized_read
expect_contains "aborts: an ASan report fails" "$out" "FAIL sanitized_read"
expect_contains "aborts: ... saying it was a sanitizer report" "$out" "stopped by a sanitizer report, which is a bug, not the stop the test expected"
expect_contains "aborts: ... and shows the child's output" "$out" "AddressSanitizer: heap-buffer-overflow"
capture build/tests $iso --test sanitized_overflow
expect_contains "aborts: a UBSan error fails" "$out" "stopped by a sanitizer report, which is a bug, not the stop the test expected"
expect_contains "aborts: ... and shows the child's output" "$out" "runtime error: signed integer overflow"
capture build/tests $iso --test abort_exit
expect_contains "aborts: exit(3) fails" "$out" "it exited with status 3"
line=$(line_of tests/test_aborts.c 'SMITH_CHECK_ABORTS(printf("child output\n"));')
expect "aborts: returning fails, and shows the child's output" "FAIL abort_returns
  tests/test_aborts.c:$line
      SMITH_CHECK_ABORTS(printf(\"child output\\n\"));
    expected an abort; it returned
    child output

0 passed, 1 failed" "$(run_plain --test abort_returns)"
capture build/tests $iso --test abort_check_inside
expect_contains "aborts: a check inside the expression is not an abort" "$out" "expected an abort; a check inside it failed"

# A crash without the sanitizers, in a project of its own.
project "$work/nosan"
cat >tests/test_crash.c <<'C'
#include <smith/test.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void test_abort_segv(void) {
    SMITH_CHECK_ABORTS((printf("before the crash\n"), raise(SIGSEGV)));
}

void test_abort_hangs(void) {
    SMITH_CHECK_ABORTS((printf("hanging\n"), sleep(30)));
}

void test_abort_exits_zero(void) {
    SMITH_CHECK_ABORTS(exit(0));
}

void test_abort_survivor(void) {
    SMITH_CHECK_ABORTS(abort());
}
C
capture "$smith" build -DSAN=0
expect "aborts: the unsanitized runner builds" 0 "$rc"
capture build/tests $iso --test abort_segv
expect_contains "aborts: a SIGSEGV fails" "$out" "FAIL abort_segv"
expect_contains "aborts: ... saying it crashed" "$out" "it crashed with SIGSEGV"
expect_contains "aborts: ... and showing what it printed first" "$out" "before the crash"
capture build/tests $iso --test abort_hangs --timeout 1
expect_contains "aborts: a hang is the test's timeout" "$out" "FAIL abort_hangs: timed out after 1."
capture build/tests $iso --test abort_exits_zero
expect_contains "aborts: exit(0) is a return, not an abort" "$out" "expected an abort; it returned"
capture build/tests $iso --test abort_ --timeout 1
expect_contains "aborts: the tests after a crash, a hang and an exit still run" "$out" "1 passed, 3 failed"
cd "$work/main" || exit 1

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
capture build/tests $iso --test overflow
expect_contains "isolation: an ASan report is shown" "$out" "AddressSanitizer: heap-buffer-overflow"
expect_contains "isolation: ... under the test's FAIL line" "$out" "FAIL overflow: killed by SIGABRT"
expect_contains "isolation: ... with the note" "$out" "    note: index 8"
capture build/tests $iso --test sleeps --timeout 1
expect_contains "isolation: a timeout is reported" "$out" "FAIL sleeps: timed out after 1."
capture build/tests $iso --test alarm_hangs
expect_contains "isolation: a test's own alarm ends it as a timeout, with the measured time" "$out" "FAIL alarm_hangs: timed out after 1."
capture build/tests $iso --test board_note
expect_contains "note: several lines show in full" "$out" "    note: board
  . x .
  x . x
  . x .
end"
expect_lacks "note: ... without the mark" "$out" "…"
capture build/tests $iso --test long_note
expect_contains "note: an overlong one ends with the mark" "$out" "aaa…"
expect_lacks "note: ... and holds no more than 4 KiB" "$out" "$(printf 'a%.0s' $(seq 1 4100))"
capture build/tests $iso --test exits
expect_contains "isolation: an exit status is reported" "$out" "FAIL exits: exited with status 3"
capture build/tests $iso --test ok_after --verbose
expect_contains "isolation: a test after the others still runs" "$out" "PASS ok_after"
capture build/tests $iso --timeout 1
expect_contains "isolation: the whole run reports every test" "$out" "failed in"
expect "isolation: ... and exits 1" 1 "$rc"
capture build/tests $iso --jobs 0
expect "options: a bad option exits 2" 2 "$rc"
expect_contains "options: ... and prints the usage" "$err" "usage:"
capture build/tests $iso --test nothing_like_this
expect "options: no matching test exits 1" "1 no test matches \"nothing_like_this\"" "$rc $out"

# 4. Running: --test filters, --seeds counts, --jobs 1 gives the same results.
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
capture build/tests $iso --test prop_ok --verbose
expect_contains "properties: a property test runs on 100 keys" "$out" "PASS prop_ok (100 seeds)"
capture build/tests $iso --test prop_ok --seeds 7 --verbose
expect_contains "seeds: --seeds sets the count" "$out" "PASS prop_ok (7 seeds)"
capture build/tests $iso --test prop_ok --seed 5 --verbose
expect_contains "seeds: --seed runs one seed" "$out" "PASS prop_ok (seed 5)"
capture "$smith" test $iso --test prop_ok --seeds 3
expect "task: smith test runs the runner" 0 "$rc"
capture "$smith" test $iso --test prop_ok --seeds x
expect "task: a bad option reaches the runner's usage, and fails the task" yes "$([ "$rc" -ne 0 ] && echo yes)"
expect_contains "task: ... which says so" "$err$out" "usage:"
capture "$smith" test $iso --test ok_after --jobs 1
expect "task: --test filters, and a passing run exits 0" 0 "$rc"
capture "$smith" test $iso --test stops
expect_contains "task: a failing test is printed" "$out$err" "FAIL stops"
expect "task: ... and the task fails" 1 "$rc"
all=$(build/tests $iso --test abort 2>&1 | plain)
one=$(build/tests $iso --test abort --jobs 1 2>&1 | plain)
expect "jobs: --jobs 1 gives the same results" "$all" "$one"

capture build/tests $iso --test prop_fails --shrink 0
seed=$(printf '%s' "$out" | sed -n 's/^FAIL prop_fails with seed \([0-9]*\).*/\1/p')
expect "properties: a failure names its seed" yes "$([ -n "$seed" ] && echo yes)"
expect_contains "properties: ... and a rerun line" "$out" "    rerun: smith test --test prop_fails --seed $seed"
# The task prints the failing line from the stack; the runner on its own prints it as the macro gave it. The seed, the value
# and the rerun line are the same either way.
first=$(printf '%s' "$out" | sed -n '/^FAIL prop_fails/,/rerun/p' | grep -E '^FAIL|^ +x +=|rerun' | sed 's/^ *//')
capture "$smith" test $iso --test prop_fails --seed "$seed" --shrink 0
second=$(printf '%s%s' "$out" "$err" | sed -n '/^FAIL prop_fails/,/rerun/p' | grep -E '^FAIL|^ +x +=|rerun' | sed 's/^ *//')
expect "properties: the rerun line reproduces the failure" "$first" "$second"
capture build/tests $iso --test prop_fails --seed "$seed" --shrink 0
third=$(printf '%s' "$out" | sed -n '/^FAIL prop_fails/,/rerun/p' | grep -E '^FAIL|^ +x +=|rerun' | sed 's/^ *//')
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
capture build/tests $iso --test shrink_int --shrink-seconds 3600
seed=$(printf '%s' "$out" | sed -n 's/^FAIL shrink_int with seed \([0-9]*\),.*/\1/p')
expect "shrink: a uniform [0, 1000] with x < 500 shrinks to 500" "FAIL shrink_int with seed $seed, shrunk in RUNS runs
  tests/test_shrink.c:$line
      SMITH_CHECK(x, <, 500);
    x = 500   (long long)
    500       (long long)
    rerun: smith test --test shrink_int --seed $seed

0 passed, 1 failed" "$(printf '%s' "$out" | plain | sed 's/shrunk in [0-9]* runs/shrunk in RUNS runs/')"
runs=$(printf '%s' "$out" | sed -n 's/.*shrunk in \([0-9]*\) runs.*/\1/p')
expect "shrink: ... in at most 1000 runs" yes "$([ -n "$runs" ] && [ "$runs" -le 1000 ] && echo yes)"

capture build/tests $iso --test shrink_int --shrink 0
unshrunk=$(printf '%s' "$out" | sed -n 's/^    x = \([0-9]*\) .*/\1/p')
expect "shrink: --shrink 0 shows the unshrunk failure" yes "$([ "$unshrunk" -gt 500 ] && echo yes)"
expect_lacks "shrink: ... without 'shrunk'" "$out" "shrunk"

capture "$smith" test $iso --test shrink_int --seed "$seed"
expect_contains "shrink: the rerun line shrinks to the same case" "$out$err" "    x = 500   (long long)"

first=$(build/tests $iso --test shrink_list --shrink-seconds 3600 2>&1 | plain)
expect_contains "shrink: a list shrinks to one item" "$first" "    note: length 1, item 0"
expect_contains "shrink: ... of 91" "$first" "    item = 91   (long long)"
second=$(build/tests $iso --test shrink_list --shrink-seconds 3600 2>&1 | plain)
expect "shrink: the same seed gives the same shrunk case" "$first" "$second"

capture build/tests $iso --test shrink_normal --shrink-seconds 3600 --seeds 5000
z=$(printf '%s' "$out" | sed -n 's/^    z *= \([0-9.e+-]*\) .*/\1/p')
expect_contains "shrink: a normal with z < 3 shrinks" "$out" "FAIL shrink_normal with seed"
expect "shrink: ... to the z nearest 3 from above" yes "$(awk -v z="$z" 'BEGIN { print (z >= 3 && z < 3.0000001) ? "yes" : "no" }')"

line=$(grep -n -F 'SMITH_CHECK(x, <, 500);' tests/test_shrink.c | sed -n 2p | cut -d: -f1)
capture build/tests $iso --test shrink_moves --shrink-seconds 3600
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
capture build/tests $iso --timeout 2 --jobs 2
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
capture build/tests $iso --test budget
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
capture build/tests $iso
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
capture build/tests $iso --list
expect "no tests: ... and adds nothing" "same" "$out"
rm tests/test_a.c
capture "$smith" build
expect "no tests: a project with none builds" 0 "$rc"
capture build/tests $iso
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
    SmithObject arena = {.compiler = "smith-cc", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
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

# 8b. Full size, SMITH_CHECK_SAME_BITS and .inputs.
project "$work/extras"
cat >tests/test_extras.c <<'C'
#include <smith/test.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

void test_size(void) {
    size_t count = smith_test_full() ? 1000 : 10;
    smith_test_note("count %zu", count);
    SMITH_CHECK(count, ==, 10);
}

void test_bits_equal(void) {
    SMITH_CHECK_SAME_BITS(0.1 + 0.2, 0.30000000000000004);
    SMITH_CHECK_SAME_BITS(-0.0, -0.0);
    SMITH_CHECK_SAME_BITS(1.5f, 1.5f);
    SMITH_CHECK_SAME_BITS(__builtin_nan(""), __builtin_nan(""));
    SMITH_CHECK_SAME_BITS(-INFINITY, -INFINITY);
}

void test_bits_zero(void) {
    double zero = 0.0;
    SMITH_CHECK_SAME_BITS(zero, -0.0);
}

void test_bits_nan(void) {
    SMITH_CHECK_SAME_BITS(__builtin_nan("1"), __builtin_nan("2"));
}

void test_bits_float(void) {
    SMITH_CHECK_SAME_BITS(1.0f, 1.0000001f);
}

void test_reads_input(void) {
    FILE *file = fopen("build/data.txt", "r");
    SMITH_CHECK(file != nullptr);
    char text[16] = "";
    SMITH_CHECK(fgets(text, sizeof text, file) != nullptr);
    fclose(file);
    smith_test_note("data %s", text);
    SMITH_CHECK(strcmp(text, "one\n"), ==, 0);
}
C
cat >gen.sh <<'S'
echo one >"$1"
S
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu23", "-g", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-I", ".smith/include", nullptr);
    SmithObject arena = {.compiler = "smith-cc", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
    smith_object(b, &arena);
    SmithObject random = {.compiler = "smith-cc", .source = smith_library(b, "random"), .output = "build/smith/random.o", .flags = flags};
    smith_object(b, &random);
    SmithStep data = {
        .command = smith_list(b, "sh", "gen.sh", "$out", nullptr),
        .implicit_inputs = smith_list(b, "gen.sh", nullptr),
        .outputs = smith_list(b, "build/data.txt", nullptr),
        .description = "generate the data",
    };
    smith_step(b, &data);
    SmithTests tests = {
        .sources = smith_list(b, "tests/test_extras.c", nullptr),
        .objects = (const SmithObject *[]){&arena, &random, nullptr},
        .flags = flags,
        .inputs = data.outputs,
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
C
capture "$smith" build
expect "extras: the runner builds" 0 "$rc"
run_extras() { # runner arguments
    "$work/extras/build/tests" $iso "$@" 2>&1 | plain
}
expect_contains "full: a test sees false by default" "$(run_extras --test size)" "1 passed, 0 failed"
capture build/tests $iso --test size --full
expect_contains "full: ... and true with --full" "$out" "note: count 1000"
capture "$smith" test $iso --test size --full
expect_contains "full: --full reaches the runner" "$out$err" "note: count 1000"
capture "$smith" test $iso --test size
expect_contains "full: ... and without it the test sees false" "$out$err" "1 passed, 0 failed"
capture build/tests $iso --test bits_equal
expect_contains "bits: equal bits pass, even a NaN and -0" "$out" "1 passed, 0 failed"
line=$(line_of tests/test_extras.c 'SMITH_CHECK_SAME_BITS(zero, -0.0);')
expect "bits: 0.0 against -0.0 fails, with both in hex" "FAIL bits_zero
  tests/test_extras.c:$line
      SMITH_CHECK_SAME_BITS(zero, -0.0);
    zero = 0 [0x0p+0]     (double)
    -0.0 = -0 [-0x0p+0]   (double)

0 passed, 1 failed" "$(run_extras --test bits_zero)"
capture build/tests $iso --test bits_nan
expect_contains "bits: two NaN payloads fail" "$out" "FAIL bits_nan"
expect_contains "bits: ... and the first NaN's bits show" "$out" "nan [0x7ff8000000000001]"
expect_contains "bits: ... and the second's" "$out" "nan [0x7ff8000000000002]"
capture build/tests $iso --test bits_float
expect_contains "bits: floats fail by their last bit, in hex" "$out" "[0x1.000002p+0]"
capture "$smith" test $iso --test reads_input
expect "inputs: the generated file is built before the tests, and read" 0 "$rc"
expect "inputs: ... by ninja" yes "$([ -f build/data.txt ] && echo yes)"
echo two >gen.sh.new && printf 'echo two >"$1"\n' >gen.sh && rm gen.sh.new
capture "$smith" test $iso --test reads_input
expect "inputs: touching the generator reruns the tests, which see the new file" 1 "$rc"
expect_contains "inputs: ... as the note shows" "$out$err" "note: data two"

# 8c. Workers: tests run one after another in a pool of long-lived processes, and --isolate gives each its own. --stats
# prints the forks the run made, which tells the two apart. A group is chosen with --test <prefix> and runs on one worker
# (--jobs 1); its tests run in the order written.
project "$work/workers"
cat >tests/test_workers.c <<'C'
#include <smith/test.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void test_crash_1_before(void) {
    SMITH_CHECK(1, ==, 1);
}

void test_crash_2_dies(void) {
    smith_test_note("about to abort");
    abort();
}

void test_crash_3_after(void) {
    SMITH_CHECK(2, ==, 2);
}

void test_crash_4_after(void) {
    SMITH_CHECK(3, ==, 3);
}

void test_late_1_before(void) {
    SMITH_CHECK(true);
}

void test_late_2_sleeps(void) {
    sleep(30);
}

void test_late_3_after(void) {
    SMITH_CHECK(true);
}

void test_report_1_before(void) {
    SMITH_CHECK(true);
}

void test_report_2_overflows(void) {
    char *bytes = malloc(8);
    volatile size_t index = 8;
    bytes[index] = 'x';
    free(bytes);
}

void test_report_3_prints_one(void) {
    printf("AddressSanitizer: a test that only says so\n");
}

void test_report_4_after(void) {
    SMITH_CHECK(true);
}

void test_aborts_1_three(void) {
    SMITH_CHECK_ABORTS(assert(false));
    SMITH_CHECK_ABORTS(abort());
    SMITH_CHECK_ABORTS(__builtin_trap());
}

void test_aborts_2_quiet(void) {
    SMITH_CHECK_ABORTS(assert(1 > 2));
    SMITH_CHECK_ABORTS((printf("hidden-by-the-check\n"), abort()));
}

void test_aborts_3_reads(void) {
    char *bytes = malloc(8);
    volatile size_t index = 8;
    SMITH_CHECK_ABORTS((printf("before the read\n"), (void)bytes[index]));
    free(bytes);
}

void test_aborts_4_after(void) {
    SMITH_CHECK_ABORTS(abort());
}

void test_unwind_1_fails(void) {
    SMITH_CHECK(1, ==, 2);
}

void test_unwind_2_fails(void) {
    SMITH_CHECK(3, ==, 4);
}

void test_unwind_3_passes(void) {
    SMITH_CHECK(true);
}

void test_out_1_fails(void) {
    printf("from-1\n");
    fprintf(stderr, "err-from-1\n");
    SMITH_CHECK(1, ==, 2);
}

void test_out_2_passes(void) {
    printf("from-2\n");
    fprintf(stderr, "err-from-2\n");
}

void test_out_3_fails(void) {
    printf("from-3\n");
    SMITH_CHECK(1, ==, 2);
}

void test_out_4_buffers(void) {
    static char buffer[256];
    setvbuf(stdout, buffer, _IOFBF, sizeof buffer);
    printf("from-4\n"); // still in the test's own buffer when the test returns
}

void test_out_5_fails(void) {
    SMITH_CHECK(1, ==, 2);
}

static int dirty_death;
static int dirty_failed;

void test_dirty_1_death(void) {
    dirty_death = 1;
    SMITH_CHECK_ABORTS(abort()); // stands for the program ending: the worker ends with it
}

void test_dirty_2_fresh(void) {
    SMITH_CHECK(dirty_death, ==, 0);
}

void test_dirty_3_fails(void) {
    dirty_failed = 1;
    SMITH_CHECK(1, ==, 2); // unwinds out of whatever it was doing: the worker ends
}

void test_dirty_4_fresh(void) {
    SMITH_CHECK(dirty_failed, ==, 0);
}

// A failure that depends on state: the first run in a process fails only for a large x, a later one always fails. Shrinking
// reruns the key in one worker, so its smaller cases fail on the state its earlier attempts left, not on their own.
static int leaky_runs;

void test_shrink_leaky(SmithKey key) {
    long long x = smith_uniform_int64_sample(key, (SmithUniformInt64){.low = 0, .high = 1000});
    leaky_runs += 1;
    bool fails = leaky_runs == 1 ? x >= 500 : true;
    SMITH_CHECK(fails, ==, false);
}

static void *fresh_base;

void test_fresh_1_sets(void) {
    smith_test_note("note from 1");
    SmithArena arena = smith_test_arena(4096);
    unsigned char *bytes = smith_arena_push(&arena, 100, 8);
    fresh_base = bytes;
    for (size_t i = 0; i < 100; i++)
        bytes[i] = 0xAB;
}

void test_fresh_2_sees(void) {
    SmithArena arena = smith_test_arena(4096);
    unsigned char *bytes = smith_arena_push(&arena, 100, 8);
    if (fresh_base != nullptr) SMITH_CHECK(bytes, ==, fresh_base); // given back: the same memory
    for (size_t i = 0; i < 100; i++)
        SMITH_CHECK(bytes[i], ==, 0);
    SMITH_CHECK(1, ==, 2); // fails to show the note it was given: none
}

void test_fresh_3_big(void) {
    SmithArena arena = smith_test_arena(200U << 20);
    SMITH_CHECK(smith_arena_push(&arena, 8, 8) != nullptr);
}

void test_fresh_4_big(void) {
    SmithArena arena = smith_test_arena(200U << 20); // out of capacity if the last test's was not given back
    SMITH_CHECK(smith_arena_push(&arena, 8, 8) != nullptr);
}

static int ran;

void test_order_a(void) {
    ran = 1;
}

void test_order_b(void) {
    SMITH_CHECK(ran, ==, 1); // only if order_a ran first, in this process
}
C
capture "$smith" build
expect "workers: the runner builds" 0 "$rc"

forks_of() { printf '%s' "$1" | sed -n 's/^forks: //p'; }
by_mode() { if [ -n "$iso" ]; then echo "$2"; else echo "$1"; fi; } # worker value, isolate value

capture build/tests $iso --test crash_ --jobs 1 --stats
expect_contains "workers: a crash midway fails its test" "$out" "FAIL crash_2_dies: killed by SIGABRT"
expect_contains "workers: ... with its note" "$out" "    note: about to abort"
expect_contains "workers: ... and the tests after it still run" "$out" "3 passed, 1 failed"
expect "workers: ... the crash cost a fresh worker (forks: 2 against one per test)" "$(by_mode 2 4)" "$(forks_of "$out")"

capture build/tests $iso --test late_ --jobs 1 --timeout 1 --stats
expect_contains "workers: a timeout fails its test" "$out" "FAIL late_2_sleeps: timed out after 1."
expect_contains "workers: ... and the tests around it pass" "$out" "2 passed, 1 failed"
expect "workers: ... it replaced the worker" "$(by_mode 2 3)" "$(forks_of "$out")"

capture build/tests $iso --test report_ --jobs 1 --stats
expect_contains "workers: a sanitizer report fails its test" "$out" "FAIL report_2_overflows: killed by SIGABRT"
expect_contains "workers: ... with the report" "$out" "AddressSanitizer: heap-buffer-overflow"
expect_contains "workers: ... and a test that only prints a report passes" "$out" "3 passed, 1 failed"
expect "workers: ... the crash and the printed report each replaced the worker" "$(by_mode 3 4)" "$(forks_of "$out")"

capture build/tests $iso --test aborts_ --jobs 1 --stats --verbose
expect_contains "workers: SMITH_CHECK_ABORTS passes" "$out" "PASS aborts_1_three"
expect_contains "workers: ... twice" "$out" "PASS aborts_2_quiet"
expect_lacks "workers: ... hiding the expression's output" "$out" "hidden-by-the-check"
expect_contains "workers: ... a sanitizer report fails the check" "$out" "FAIL aborts_3_reads"
expect_contains "workers: ... saying so" "$out" "expected an abort; it was stopped by a sanitizer report"
expect_contains "workers: ... with the report" "$out" "AddressSanitizer: heap-buffer-overflow"
expect_contains "workers: ... and shows what the expression printed" "$out" "before the read"
expect_contains "workers: ... a later check passes" "$out" "PASS aborts_4_after"
expect "workers: ... in-process, the checks forked nothing: a test with a death check ends its worker, one fork a test" "$(by_mode 4 11)" "$(forks_of "$out")"

capture build/tests $iso --test unwind_ --jobs 1 --stats
expect_contains "workers: failed checks fail their tests" "$out" "1 passed, 2 failed"
expect "workers: ... and a failed test ends its worker, so each test here has its own" 3 "$(forks_of "$out")"

seed=
for number in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    capture build/tests $iso --test shrink_leaky --seed $number --shrink 0
    case $out in *"FAIL shrink_leaky"*) seed=$number; break ;; esac
done
expect "leaky: some seed fails the first run" yes "$([ -n "$seed" ] && echo yes)"
capture build/tests $iso --test shrink_leaky --seed "${seed:-1}"
if [ -n "$iso" ]; then
    expect_contains "leaky: with a process per attempt nothing leaks, and it shrinks to 500" "$out" "shrunk in"
    expect_lacks "leaky: ... with no leak line" "$out" "did not reproduce"
else
    expect_contains "leaky: a shrunk case that fails only on leaked state is not reported as shrunk" "$out" "the shrunk case did not reproduce in a fresh process, so state leaked between the shrinking attempts"
    expect_lacks "leaky: ... the failure is shown as it was" "$out" "shrunk in"
    expect_contains "leaky: ... and still names the seed" "$out" "rerun: smith test --test shrink_leaky --seed ${seed:-1}"
fi

capture build/tests $iso --test dirty_ --jobs 1 --stats
expect_contains "dirty: a test after a death check sees clean globals, and after a failed test too" "$out" "3 passed, 1 failed"
expect_contains "dirty: ... only the failing test failed" "$out" "FAIL dirty_3_fails"
expect "dirty: ... the death check and the failure each ended a worker" "$(by_mode 3 5)" "$(forks_of "$out")"

capture build/tests $iso --test out_ --jobs 1 --stats
first=$(printf '%s\n' "$out" | sed -n '/^FAIL out_1_fails/,/^FAIL out_3_fails/p')
third=$(printf '%s\n' "$out" | sed -n '/^FAIL out_3_fails/,/^FAIL out_5_fails/p')
fifth=$(printf '%s\n' "$out" | sed -n '/^FAIL out_5_fails/,$p')
expect_contains "output: a failing test shows its own output" "$first" "from-1"
expect_contains "output: ... and its stderr" "$first" "err-from-1"
expect_lacks "output: ... a passing test's output stays out of the next failure" "$third" "from-2"
expect_lacks "output: ... and the next one's" "$third" "err-from-2"
expect_contains "output: ... the next failure shows its own" "$third" "from-3"
expect_lacks "output: ... a test's output still in its own buffer does not reach the next test" "$fifth" "from-4"
expect_lacks "output: ... nor does an earlier one" "$fifth" "from-3"
expect_lacks "output: ... the last failure has no output at all" "$fifth" "    note"

capture build/tests $iso --test fresh_ --jobs 1 --stats
expect_contains "fresh: the arena and the note start empty (the second test's failure is its own)" "$out" "FAIL fresh_2_sees"
expect_lacks "fresh: ... no note from the earlier test" "$out" "note from 1"
expect_contains "fresh: ... and arenas of 200 MiB are given back between tests" "$out" "3 passed, 1 failed"

capture build/tests $iso --test order_ --jobs 1 --verbose
if [ -n "$iso" ]; then
    expect_contains "shuffle: with a process per test an order-dependent pair fails even in order" "$out" "FAIL order_b"
else
    expect_contains "shuffle: in table order the order-dependent pair passes" "$out" "2 passed, 0 failed"
    found=
    for number in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
        capture build/tests --test order_ --jobs 1 --shuffle $number --verbose
        case $out in *"FAIL order_b"*) found=$number; break ;; esac
    done
    expect "shuffle: some seed runs order_b before order_a, and the test fails" yes "$([ -n "$found" ] && echo yes)"
    first=$(build/tests --test order_ --jobs 1 --shuffle "${found:-1}" --verbose 2>&1 | plain)
    second=$(build/tests --test order_ --jobs 1 --shuffle "${found:-1}" --verbose 2>&1 | plain)
    expect "shuffle: the same seed gives the same results" "$first" "$second"
    expect_contains "shuffle: ... reported in table order" "$first" "PASS order_a
FAIL order_b"
    capture "$smith" test --test order_ --jobs 1 --shuffle "${found:-1}"
    expect_contains "shuffle: --shuffle reaches the runner" "$out$err" "FAIL order_b"
fi
cd "$work/main" || exit 1

# Many jobs under a low descriptor limit: every job's worker holds two descriptors in the runner, which raises the limit.
project "$work/many"
{
    echo '#include <smith/test.h>'
    echo '#include <unistd.h>'
    for number in $(seq 1 90); do
        printf 'void test_many_%s(void) {\n    usleep(200000);\n}\n' "$number"
    done
} >tests/test_many.c
capture "$smith" build -DSAN=0
expect "many: the runner builds" 0 "$rc"
capture sh -c "ulimit -S -n 100; build/tests $iso --jobs 90"
expect_contains "many: 90 jobs under a limit of 100 descriptors all run" "$out" "90 passed, 0 failed"
cd "$work/main" || exit 1

# 9. Tools: clangd reads a test file without errors.
cd "$work/main" || exit 1
if "$smith" lsp --version >/dev/null 2>&1; then
    capture "$smith" build
    capture "$smith" lsp --check=tests/test_checks.c
    errors=$(printf '%s\n%s\n' "$out" "$err" | grep -c 'E\[')
    expect "clangd: no errors in a test file" 0 "$errors"
fi

# 10. Failure reports: a failure is a stack of addresses, and smith turns it into the failing line and the call chain.
# The plain-function checks print no file and line themselves, so the runner on its own prints the raw trace; smith
# symbolize (and smith test, which pipes the runner through it) reads the source.
project "$work/reports"
cat >tests/test_reports.c <<'C'
#include <smith/test.h>

#include <assert.h>

[[gnu::always_inline]] static inline void check_positive(int value) {
    smith_check(value > 0);
}

void test_passes(void) {
    smith_check(true);
    smith_check_equal_u64(4, 4);
    smith_check_equal_f64(0.5, 0.5);
    smith_check_near_f32(1.0F, 1.0001F, 0.001F);
    smith_check_same_bits_f64(-0.0, -0.0);
}

void test_check_fails(void) {
    int moves = 0;
    smith_check(moves > 0);
}

void test_equal_fails(void) {
    uint64_t count = 3;
    uint64_t expected = 4;
    smith_check_equal_u64(count, expected);
}

void test_near_fails(void) {
    smith_check_near_f64(1.0, 1.5, 0.1);
}

void test_same_bits_fails(void) {
    smith_check_same_bits_f32(0.0F, -0.0F);
}

void test_assert_fails(void) {
    int moves = 1;
    assert(moves == 2);
}

void test_segfault(void) {
    volatile int *nothing = nullptr;
    *nothing = 7;
}

void test_inlined(void) {
    check_positive(-1);
}

void test_macro_fails(void) {
    SMITH_CHECK(2 + 2, ==, 5);
}

void test_prop_function(SmithKey key) {
    int64_t x = smith_uniform_int64_sample(key, (SmithUniformInt64){.low = 0, .high = 1000});
    smith_test_note("x = %lld", (long long)x);
    smith_check(x < 500);
}
C
capture "$smith" build -DSAN=0
expect "reports: the runner builds" 0 "$rc"
capture build/tests $iso --test passes --verbose
expect_contains "reports: the plain-function checks pass" "$out" "PASS passes"

# Without smith in the pipe, a failure prints the trace and the values, and a macro keeps its old report.
capture build/tests $iso --test check_fails
expect_contains "raw: a failed smith_check prints a trace block" "$out" "  smith-trace begin check failed"
expect_contains "raw: ... with frames" "$out" "  smith-trace frame 0x"
expect_contains "raw: ... and the image's UUID" "$out" "  smith-trace image "
capture build/tests $iso --test equal_fails
expect_contains "raw: a failed check_equal shows a" "$out" "    a = 3   (u64)"
expect_contains "raw: ... and b" "$out" "    b = 4   (u64)"
line=$(line_of tests/test_reports.c 'SMITH_CHECK(2 + 2, ==, 5);')
expect "raw: a macro keeps the report it had" "FAIL macro_fails
  tests/test_reports.c:$line
      SMITH_CHECK(2 + 2, ==, 5);
    2 + 2 = 4   (int)
    5           (int)

0 passed, 1 failed" "$(build/tests $iso --test macro_fails 2>&1 | plain)"
capture build/tests $iso --test prop_function --shrink-seconds 3600
expect_contains "raw: a property test's failed function check shrinks" "$out" "shrunk in"
expect_contains "raw: ... to the smallest case" "$out" "    note: x = 500"
capture build/tests $iso --test near_fails
expect_contains "raw: a failed check_near shows the distance" "$out" "    distance  = 0.5 [0x1p-1]"
capture build/tests $iso --test same_bits_fails
expect_contains "raw: a failed check_same_bits shows the bits" "$out" "    a = 0 [0x0p+0]"
expect "reports: --list sees the new tests" yes "$(build/tests --list | grep -q segfault && echo yes)"

if "$smith" symbolize --available; then
    sym() { # test name: the report smith test prints
        capture "$smith" test $iso -DSAN=0 --test "$1"
        printf '%s\n%s\n' "$out" "$err"
    }
    line=$(line_of tests/test_reports.c 'smith_check(moves > 0);')
    report=$(sym check_fails)
    expect_contains "report: a failed smith_check names the line" "$report" "  check failed at tests/test_reports.c:$line"
    expect_contains "report: ... shows the source line" "$report" "$line |     smith_check(moves > 0);"
    expect_contains "report: ... and the call chain" "$report" "  called from"
    expect_contains "report: ... whose first frame is the test" "$report" "tests/test_reports.c:$line"
    expect_contains "report: ... and whose last is the runner" "$report" "smith_test_execute"
    expect_lacks "report: ... without smith's own check machinery" "$report" "smith_check "
    expect_lacks "report: ... or the raw trace" "$report" "smith-trace"

    line=$(line_of tests/test_reports.c 'smith_check_equal_u64(count, expected);')
    report=$(sym equal_fails)
    expect_contains "report: a failed check_equal names the line" "$report" "  check failed at tests/test_reports.c:$line"
    expect_contains "report: ... and prints both values under the source line, named by their text" "$report" "$line |     smith_check_equal_u64(count, expected);
            count = 3, expected = 4   (u64)
  called from"

    line=$(line_of tests/test_reports.c 'assert(moves == 2);')
    report=$(sym assert_fails)
    expect_contains "report: an assert names the line" "$report" "  aborted at tests/test_reports.c:$line"
    expect_contains "report: ... shows it" "$report" "$line |     assert(moves == 2);"
    expect_contains "report: ... and keeps assert's own message" "$report" "Assertion failed: (moves == 2)"
    expect_lacks "report: ... without libc's abort path" "$report" "pthread_kill"

    line=$(line_of tests/test_reports.c '*nothing = 7;')
    report=$(sym segfault)
    expect_contains "report: a segfault names the line" "$report" "FAIL segfault: killed by SIGSEGV
  crashed at tests/test_reports.c:$line"
    expect_contains "report: ... shows it" "$report" "$line |     *nothing = 7;"

    line=$(line_of tests/test_reports.c 'smith_check(value > 0);')
    caller=$(line_of tests/test_reports.c 'check_positive(-1);')
    report=$(sym inlined)
    expect_contains "report: an inlined check names the inlined line" "$report" "  check failed at tests/test_reports.c:$line"
    expect_contains "report: ... and the chain has the inlined function" "$report" "check_positive"
    expect_contains "report: ... and the function it was inlined into" "$report" "tests/test_reports.c:$caller"

    line=$(line_of tests/test_reports.c 'SMITH_CHECK(2 + 2, ==, 5);')
    report=$(sym macro_fails)
    expect_contains "report: a macro's failure has the same report" "$report" "  check failed at tests/test_reports.c:$line"
    expect_contains "report: ... and its values" "$report" "    2 + 2 = 4   (int)"

    # Objective-C frames and methods.
    project "$work/objc"
    sed -e 's|tests/test_\*.c|tests/test_*.m|' -e 's|\.link_flags = .*|.link_flags = smith_list(b, "-framework", "Foundation", nullptr),|' build.c >build.c.new && mv build.c.new build.c
    cat >tests/test_objc.m <<'M'
#import <Foundation/Foundation.h>

#include <smith/test.h>

@interface Probe : NSObject
- (void)requirePositive:(int)value;
@end

@implementation Probe
- (void)requirePositive:(int)value {
    smith_check(value > 0);
}
@end

void test_objc_fails(void) {
    Probe *probe = [[Probe alloc] init];
    [probe requirePositive:-1];
}
M
    line=$(line_of tests/test_objc.m 'smith_check(value > 0);')
    capture "$smith" test $iso -DSAN=0
    expect_contains "objc: a check inside a method names the line" "$out$err" "  check failed at tests/test_objc.m:$line"
    expect_contains "objc: ... and the method is in the chain" "$out$err" "-[Probe requirePositive:]"
    expect_contains "objc: ... called from the test" "$out$err" "test_objc_fails"

    cd "$work/reports" || exit 1
    # The Apple linker, in place of lld: the same debug map.
    rm -f build/tests
    line=$(line_of tests/test_reports.c 'smith_check(moves > 0);')
    capture env SMITH_LINKER=/usr/bin/ld "$smith" test $iso -DSAN=0 --test check_fails
    expect_contains "report: a program linked by Apple's ld has lines too" "$out$err" "  check failed at tests/test_reports.c:$line"
    rm -f build/tests

    # A trace saved from a run is symbolized later, given the program.
    capture "$smith" build -DSAN=0
    capture sh -c "build/tests $iso --test check_fails --trace >'$work/trace.txt' 2>&1"
    saved=$("$smith" symbolize "$work/trace.txt")
    expect_contains "symbolize: a saved trace becomes a report" "$saved" "  check failed at tests/test_reports.c:$line"
    expect_contains "symbolize: ... from standard input too" "$("$smith" symbolize <"$work/trace.txt")" "called from"
    cp build/tests "$work/moved_tests"
    mv build/tests build/tests_gone
    capture "$smith" symbolize "$work/trace.txt"
    expect_contains "symbolize: a program that is gone is said so" "$out" "note: cannot read"
    capture "$smith" symbolize --binary "$work/moved_tests" "$work/trace.txt"
    expect_contains "symbolize: --binary names it, and its UUID is checked" "$out" "  check failed at tests/test_reports.c:$line"
    mv build/tests_gone build/tests

    # A source file newer than the program may have changed since: the report says so.
    sleep 1
    touch tests/test_reports.c
    capture sh -c "build/tests $iso --test check_fails --trace 2>&1 | '$smith' symbolize"
    expect_contains "newer: a source newer than the program is noted" "$out" "note: tests/test_reports.c is newer than the program, so line $line may have changed"

    # A trace of another program: the UUID differs, and no line is made up.
    printf '\n' >>tests/test_reports.c
    printf 'void test_extra(void) {\n    smith_check(true);\n}\n' >>tests/test_reports.c
    capture "$smith" build -DSAN=0
    capture "$smith" symbolize "$work/trace.txt"
    expect_contains "symbolize: a program that is not the one that ran is noted" "$out" "is not the program that ran"
    expect_lacks "symbolize: ... and no line is claimed" "$out" "check failed at tests/"

    # smith as llvm-symbolizer: the sanitizers' protocol.
    address=$(nm build/tests | awk '$3 == "_test_check_fails" {print $1}')
    ln -s "$smith" "$work/llvm-symbolizer"
    capture sh -c "printf 'CODE \"%s:arm64\" 0x%s\n' '$PWD/build/tests' '$address' | '$work/llvm-symbolizer' --demangle --inlines"
    expect_contains "protocol: a CODE request names the function" "$out" "test_check_fails"
    expect_contains "protocol: ... and the file, line and column" "$out" "tests/test_reports.c:"
    capture sh -c "printf '\"%s\" 0x1\n' '/nonexistent' | '$work/llvm-symbolizer'"
    expect "protocol: an unknown module gives ?? and an empty line" "??
??:0:0
" "$out
"
fi

# A failed contract is a failure like a failed check: smith_failure gives it the same report, and a SMITH_CHECK_ABORTS
# still passes on it, as it does on an assert.
if "$smith" symbolize --available; then
    project "$work/contracts"
    {
        sed -e '/^    SmithTests tests = {/,$d' build.c
        cat <<'C'
    SmithObject contract = {.compiler = "smith", .source = smith_library(b, "contract"), .output = "build/smith/contract.o", .flags = flags};
    smith_object(b, &contract);
    SmithTests tests = {
        .sources = smith_glob(b, smith_list(b, "tests/test_*.c", nullptr)),
        .objects = (const SmithObject *[]){&arena, &random, &contract, nullptr},
        .flags = flags,
        .link_flags = smith_when(b, sanitize, smith_list(b, "-fsanitize=address,undefined", nullptr)),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
C
    } >build.c.new && mv build.c.new build.c
    cat >tests/test_contracts.c <<'C'
#include <smith/contract.h>
#include <smith/test.h>

static int halve(int value) {
    smith_requires(value % 2 == 0);
    return value / 2;
}

void test_requires_fails(void) {
    int odd = 7;
    (void)halve(odd);
}

void test_requires_aborts(void) {
    SMITH_CHECK_ABORTS((void)halve(5));
}

void test_requires_holds(void) {
    smith_check_equal_i32(halve(8), 4);
}
C
    capture "$smith" test $iso -DSAN=0
    line=$(line_of tests/test_contracts.c 'smith_requires(value % 2 == 0);')
    expect_contains "contract: a failed smith_requires names its line" "$out$err" "  requires failed at tests/test_contracts.c:$line"
    expect_contains "contract: ... shows the source" "$out$err" "$line |     smith_requires(value % 2 == 0);"
    caller=$(line_of tests/test_contracts.c '(void)halve(odd);')
    expect_contains "contract: ... and the call chain, with the caller" "$out$err" "tests/test_contracts.c:$caller"
    expect_contains "contract: SMITH_CHECK_ABORTS passes on a failed contract, and the true one passes" "$out$err" "2 passed, 1 failed"
    expect_lacks "contract: ... with no smith_failure frame in the chain" "$out$err" "smith_failure"
    cd "$work/main" || exit 1
fi

# A sanitizer's report is symbolized by smith, through the llvm-symbolizer protocol, and AddressSanitizer's own stack is
# what the failure shows.
if "$smith" symbolize --available; then
    project "$work/sanitized"
    cat >tests/test_overflow.c <<'C'
#include <smith/test.h>

#include <stdlib.h>

static int read_past(const int *values, int count) {
    return values[count];
}

void test_overflow(void) {
    int *values = malloc(4 * sizeof(int));
    for (int i = 0; i < 4; i++)
        values[i] = i;
    volatile int sum = read_past(values, 4);
    (void)sum;
    free(values);
}

void test_signed_overflow(void) {
    volatile int big = 2147483647;
    volatile int more = big + 1;
    (void)more;
}
C
    line=$(line_of tests/test_overflow.c 'return values[count];')
    capture "$smith" test $iso --test overflow
    expect_contains "asan: the report's innermost user frame is named" "$out$err" "in read_past "
    expect_contains "asan: ... with its line" "$out$err" "tests/test_overflow.c:$line"
    expect_contains "asan: ... and the caller's" "$out$err" "in test_overflow "
    expect_lacks "asan: ... with no module offsets, which atos gives without a dSYM" "$out$err" "tests:arm64+0x"
    line=$(line_of tests/test_overflow.c 'volatile int more = big + 1;')
    capture "$smith" test $iso --test signed_overflow
    expect_contains "ubsan: the report names the line" "$out$err" "tests/test_overflow.c:$line"
fi
cd "$work/main" || exit 1

# 11. The libraries compile clean under the flags `smith new` writes, in both C standards, and under the extra
# warnings game and learner add, since every project compiles them with its own flags.
flags=$(awk '/build_warnings\[\] =/ {on = 1} on {print} /nullptr};/ {on = 0}' "$repo/templates/new/build.c" | grep -o '"-[^"]*"' | tr -d '"' | grep -v '^-std=')
flags="$flags -Wdouble-promotion -Wundef -Wpointer-arith -Wcast-qual -Wformat=2 -Wmissing-variable-declarations -Wcomma -Wunreachable-code"
for standard in gnu2y gnu23; do
    for library in test random arena trace; do
        # shellcheck disable=SC2086
        capture /opt/homebrew/opt/llvm/bin/clang -std=$standard $flags -I "$repo/include" -c "$repo/lib/$library.c" -o "$work/$library.o"
        expect "flags: lib/$library.c compiles clean under $standard with the template's warnings" 0 "$rc"
        [ "$rc" -eq 0 ] || printf '%s\n%s\n' "$out" "$err"
    done
done

echo
echo "$total test checks, $failures failed"
[ "$failures" -eq 0 ]
