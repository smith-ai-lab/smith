#!/bin/sh
# Tests of <smith/test.h>, smith_tests and the runner, with real clang, ninja and clangd.
# Usage: sh tests/test.sh path/to/smith [isolate] [parts]
# The runner's workers are the default; with "isolate" every runner is run with --isolate (`smith test --isolate`), the
# process per test, and every check must give the same result.
# parts is a comma-separated list of the parts to run (default all): the file is in parts that each work in projects of
# their own, so that build.c can run them as tasks side by side (main, shrink_int, shrink_list, shrink_normal, alongside,
# budget, misc, workers, reports, flags, inprocess, lang).
# A scratch project builds a runner from test files that fail on purpose, and each case runs it and compares the output.
set -u
# `--parts` prints the names of the parts, one a line: build.c checks them against its task rows.
if [ "${1:-}" = --parts ]; then
    sed -n 's/^if part \(.*\); then$/\1/p' "$0" | tr ' ' '\n' | sort -u
    exit 0
fi
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
iso=
if [ "${2:-}" = isolate ]; then
    iso=--isolate
    shift
fi
parts=${2:-all}
for name in $(echo "$parts" | tr ',' ' '); do # a name that is no part would run nothing, silently
    [ "$name" = all ] && continue
    if ! sh "$0" --parts | grep -qx "$name"; then
        echo "tests/test.sh: '$name' is not a part (the parts: $(sh "$0" --parts | tr '\n' ' '))" >&2
        exit 2
    fi
done
part() { # name...: whether this run includes any of the parts
    for name in "$@"; do
        case ",$parts," in *,all,* | *",$name,"*) return 0 ;; esac
    done
    return 1
}
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
    const char *const *flags = smith_cat(b, smith_list(b, "-std=gnu2y", "-g", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-I", ".smith/include", nullptr),
                                         smith_when(b, sanitize, smith_list(b, "-fsanitize=address,undefined", "-fno-sanitize-recover=all", nullptr)), nullptr);
    SmithObject arena = {.compiler = "smith-cc", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
    smith_object(b, &arena);
    SmithObject random = {.compiler = "smith-cc", .source = smith_library(b, "random"), .output = "build/smith/random.o", .flags = flags};
    smith_object(b, &random);
    SmithTests tests = {
        .sources = smith_glob(b, smith_list(b, "tests/test_*.c", "tests/test_*.smith", nullptr)),
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

# A project of C tests only, in C23: the C API of <smith/test.h> with no .smith file anywhere.
project_c() { # folder
    project "$1"
    sed -e 's/gnu2y/gnu23/' -e 's|, "tests/test_\*.smith"||' build.c >build.c.new && mv build.c.new build.c
}

# The runner's output without the timings: the slowest list is dropped and the summary's seconds are masked.
plain() {
    sed -e '/^slowest:/,/^$/d' -e 's/ in [0-9.]* s$//' | sed '/./,$!d'
}

run_plain() { # runner arguments
    "$work/main/build/tests" $iso "$@" 2>&1 | plain
}

# The report `smith test` prints for a failure: the runner's trace turned into the failing line and the call chain by smith
# symbolize, from the project's folder, without the timings and the runner's own frame (its line moves with the library).
# The spaces are squeezed on both sides, because the columns of the chain follow the longest path in it.
have_symbolizer=$("$smith" symbolize --available && echo yes)
report() { # runner arguments
    build/tests $iso --trace "$@" 2>&1 | "$smith" symbolize 2>&1 | plain | sed -e '/smith_test_execute/d' -e '/smith_test_run_key/d'
}
expect_report() { # name, expected report, runner arguments
    name=$1
    wanted=$(printf '%s\n' "$2" | tr -s ' ')
    shift 2
    expect "$name" "$wanted" "$(report "$@" | tr -s ' ')"
}

line_of() { # file, text: the line number of the first line holding the text
    grep -n -F "$2" "$1" | head -n 1 | cut -d: -f1
}

if part main; then
# 1. The checks: a project with passing and failing tests, in .smith, and a few in C for the C API.
project "$work/main"
cat >tests/test_checks.smith <<'C'
import <assert.h>;
import <stdio.h>;
import smith::math;
import smith::test;

void test_passing(void) {
    c::printf("this output is hidden\n");
    test::check_equal(1 + 1, 2);
    test::check(true);
    test::check_near(0.1 + 0.2, 0.3, 1e-9);
    test::check_near(1.0f, 1.0001f, 0.001f);
    i32 array[3] = {1, 2, 3};
    test::check_equal(array, &array[0]);
    test::expect_abort();
    c::assert(false);
}

void test_compare(void) {
    i64 x = 7;
    struct {
        i64 high;
    } range = {6};
    c::printf("the test's output\n");
    test::note("loop %d", 3);
    test::check_less_equal(x, range.high);
}

void test_condition(void) {
    i32 items = 0;
    test::check(items > 0);
}

void test_unsigned(void) {
    u32 small = 3;
    test::check_greater_equal(small, 4);
}

void test_float_bits(void) {
    test::check_equal(0.1 + 0.2, 0.3);
}

void test_char(void) {
    char letter = 'a';
    test::check_equal(letter, 'b');
}

void test_bool(void) {
    bool flag = true;
    test::check_equal(flag, false);
}

void test_pointer(void) {
    i32 value = 0;
    i32 *pointer = &value;
    test::check_equal(pointer, nullptr);
}

void test_near(void) {
    f64 low = 1.0;
    f64 high = 1.5;
    f64 tolerance = 0.1;
    test::check_near(low, high, tolerance);
}

void test_near_nan(void) {
    f64 nan = math::nan(f64);
    test::check_near(nan, 1.0, 1e300);
}

void test_stops(void) {
    test::check_equal(1, 2);
    c::printf("never printed\n");
}
C
cat >tests/test_checks_c.c <<'C'
#include <smith/test.h>

#include <stdio.h>

void test_c_passing(void) {
    printf("this output is hidden\n");
    smith_check(true);
    smith_check_equal_i32(1 + 1, 2);
    smith_check_near_f64(0.1 + 0.2, 0.3, 1e-9);
    smith_check_same_bits_f32(1.5F, 1.5F);
}

void test_c_equal(void) {
    long long x = 7;
    smith_check_equal_i64(x, 6);
}

void test_c_near(void) {
    smith_check_near_f64(1.0, 1.5, 0.1);
}

void test_c_pointer(void) {
    int value = 0;
    int *pointer = &value;
    smith_check_equal_ptr(pointer, nullptr);
}
C
capture "$smith" build
expect "build: the runner builds" 0 "$rc"
expect "build: the scan writes the prototypes" yes "$([ -f build/tests_list.h ] && echo yes)"
expect "build: ... and the table" yes "$([ -f build/tests_list.c ] && echo yes)"
expect_contains "build: the table lists a test by its name" "$(cat build/tests_list.c)" '"compare", test_compare'
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
stops
c_passing
c_equal
c_near
c_pointer" "$out"

expect "pass: the line and the summary, nothing else" "PASS passing

1 passed, 0 failed" "$(run_plain --test passing --verbose)"
expect "pass: without --verbose only the summary" "1 passed, 0 failed" "$(run_plain --test passing)"
expect "pass: the C names pass too, and hide the output" "PASS c_passing

1 passed, 0 failed" "$(run_plain --test c_passing --verbose)"

# The runner on its own prints a failure as a raw trace and the values, with a and b for the names; smith symbolize reads the
# trace and the source line, which also names the values by their text.
capture build/tests $iso --test compare
expect_contains "raw: a failed check prints a trace block" "$out" "  smith-trace begin check failed"
expect_contains "raw: ... with frames" "$out" "  smith-trace frame 0x"
expect_contains "raw: ... and the image's UUID" "$out" "  smith-trace image "
expect_contains "raw: ... both values with their type, named a and b" "$out" "    a = 7   (i64)
    b = 6   (i64)"
expect_contains "raw: ... then the note and the test's output" "$out" "    note: loop 3
    the test's output"

if [ -n "$have_symbolizer" ]; then
    line=$(line_of tests/test_checks.smith 'test::check_less_equal(x, range.high);')
    expect_report "compare: the failure block" "FAIL compare
  check failed at tests/test_checks.smith:$line
     $line |     test::check_less_equal(x, range.high);
           x = 7, range.high = 6   (i64)
  called from
    tests/test_checks.smith:$line test_compare
    note: loop 3
    the test's output

0 passed, 1 failed" --test compare

    line=$(line_of tests/test_checks.smith 'test::check(items > 0);')
    expect_report "condition: the failure block" "FAIL condition
  check failed at tests/test_checks.smith:$line
     $line |     test::check(items > 0);
           the condition is false
  called from
    tests/test_checks.smith:$line test_condition

0 passed, 1 failed" --test condition

    line=$(line_of tests/test_checks.smith 'test::check_greater_equal(small, 4);')
    expect_report "types: a u32 prints as one, and a literal has no =" "FAIL unsigned
  check failed at tests/test_checks.smith:$line
     $line |     test::check_greater_equal(small, 4);
           small = 3, 4   (u32)
  called from
    tests/test_checks.smith:$line test_unsigned

0 passed, 1 failed" --test unsigned

    line=$(line_of tests/test_checks.smith 'test::check_equal(0.1 + 0.2, 0.3);')
    expect_report "types: an f64 prints its shortest text and its hex, so the last bit shows" "FAIL float_bits
  check failed at tests/test_checks.smith:$line
     $line |     test::check_equal(0.1 + 0.2, 0.3);
           0.1 + 0.2 = 0.30000000000000004 [0x1.3333333333334p-2], 0.3 = 0.3 [0x1.3333333333333p-2]   (f64)
  called from
    tests/test_checks.smith:$line test_float_bits

0 passed, 1 failed" --test float_bits

    line=$(line_of tests/test_checks.smith "test::check_equal(letter, 'b');")
    expect_report "types: a char prints as a character" "FAIL char
  check failed at tests/test_checks.smith:$line
     $line |     test::check_equal(letter, 'b');
           letter = 'a', 'b'   (char)
  called from
    tests/test_checks.smith:$line test_char

0 passed, 1 failed" --test char

    line=$(line_of tests/test_checks.smith 'test::check_equal(flag, false);')
    expect_report "types: a bool prints true and false" "FAIL bool
  check failed at tests/test_checks.smith:$line
     $line |     test::check_equal(flag, false);
           flag = true, false   (bool)
  called from
    tests/test_checks.smith:$line test_bool

0 passed, 1 failed" --test bool

    capture sh -c "build/tests $iso --test pointer --trace 2>&1 | '$smith' symbolize"
    expect_contains "types: a pointer prints as an address" "$out" "pointer = 0x"
    expect_contains "types: ... and nullptr as nullptr" "$out" ", nullptr   (ptr)"

    line=$(line_of tests/test_checks.smith 'test::check_near(low, high, tolerance);')
    capture sh -c "build/tests $iso --test near --trace 2>&1 | '$smith' symbolize"
    expect_contains "near: the failure names the line" "$out" "  check failed at tests/test_checks.smith:$line"
    expect_contains "near: ... shows the first value" "$out" "= 1 [0x1p+0]   (f64)"
    expect_contains "near: ... the second" "$out" "= 1.5 [0x1.8p+0]   (f64)"
    expect_contains "near: ... the tolerance" "$out" "= 0.1 [0x1.999999999999ap-4]   (f64)"
    expect_contains "near: ... and the distance" "$out" "distance"
    expect_contains "near: ... which is 0.5" "$out" "= 0.5 [0x1p-1]   (f64)"

    line=$(line_of tests/test_checks_c.c 'smith_check_equal_i64(x, 6);')
    expect_report "c: smith_check_equal_i64 prints the same block, named by the text" "FAIL c_equal
  check failed at tests/test_checks_c.c:$line
     $line |     smith_check_equal_i64(x, 6);
           x = 7, 6   (i64)
  called from
    tests/test_checks_c.c:$line test_c_equal

0 passed, 1 failed" --test c_equal

    line=$(line_of tests/test_checks_c.c 'smith_check_near_f64(1.0, 1.5, 0.1);')
    capture sh -c "build/tests $iso --test c_near --trace 2>&1 | '$smith' symbolize"
    expect_contains "c: smith_check_near_f64 names its line" "$out" "  check failed at tests/test_checks_c.c:$line"
    expect_contains "c: ... and the distance" "$out" "= 0.5 [0x1p-1]   (f64)"
fi

capture build/tests $iso --test near_nan
expect_contains "near: a NaN fails" "$out" "FAIL near_nan"
expect_contains "near: ... and prints the NaN" "$out" "= nan"

capture build/tests $iso --test stops
expect_lacks "stop: a failed check ends the test" "$out" "never printed"

# 2. Aborts.
cat >tests/test_aborts.smith <<'C'
import <assert.h>;
import <stdio.h>;
import <stdlib.h>;
import smith::arena;
import smith::test;

void test_abort_assert(void) {
    test::expect_abort();
    c::printf("child noise\n");
    c::assert(1 > 2);
}

void test_abort_arena(void) {
    arena::Arena memory = test::memory(128);
    test::expect_abort();
    (void)arena::push_array(&memory, u8, 1000);
}

void test_abort_abort(void) {
    test::expect_abort();
    c::abort();
}

void test_abort_trap(void) {
    test::expect_abort();
    __builtin_trap();
}

void test_sanitized_read(void) {
    u8 *bytes = (u8 *)c::malloc(8);
    volatile usize index = 8;
    test::expect_abort();
    c::printf("before\n");
    (void)bytes[index];
    c::free(bytes);
}

void test_sanitized_overflow(void) {
    volatile i32 largest = 2147483647;
    test::expect_abort();
    (void)(largest + 1);
}

void test_abort_exit(void) {
    test::expect_abort();
    c::exit(3);
}

void test_abort_returns(void) {
    test::expect_abort();
    c::printf("child output\n");
}

void test_abort_check_inside(void) {
    test::expect_abort();
    test::check(false);
}

void test_abort_twice(void) {
    test::expect_abort();
    test::expect_abort(); // harmless
    c::abort();
}

void test_abort_before(void) {
    c::printf("aborting early\n");
    c::abort();
    test::expect_abort();
}

void test_abort_after_check(void) {
    test::check_equal(1 + 1, 2);
    test::expect_abort();
    c::abort();
}
C
capture "$smith" build
expect "aborts: the runner builds" 0 "$rc"
capture build/tests $iso --test abort_assert --verbose
expect_contains "aborts: an abort passes" "$out" "PASS abort_assert"
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
line=$(($(line_of tests/test_aborts.smith 'c::printf("child output\n");') - 1)) # the call, the line above the print
capture build/tests $iso --test abort_returns
expect_contains "aborts: returning fails" "$out" "FAIL abort_returns"
expect_contains "aborts: ... saying it returned" "$out" "expected an abort; the test returned"
expect_contains "aborts: ... and shows the test's output" "$out" "child output"
if [ -n "$have_symbolizer" ]; then
    capture sh -c "build/tests $iso --test abort_returns --trace 2>&1 | '$smith' symbolize"
    expect_contains "aborts: ... at the call's line" "$out" "  check failed at tests/test_aborts.smith:$line"
    expect_contains "aborts: ... showing the call" "$out" "$line |     test::expect_abort();"
fi
capture build/tests $iso --test abort_check_inside
expect_contains "aborts: a check that fails after the call is not an abort" "$out" "expected an abort; a check failed, which is not an abort"
capture build/tests $iso --test abort_twice --verbose
expect_contains "aborts: a second call is harmless" "$out" "PASS abort_twice"
capture build/tests $iso --test abort_after_check --verbose
expect_contains "aborts: checks before the call run as always" "$out" "PASS abort_after_check"
capture build/tests $iso --test abort_before
expect_contains "aborts: an abort before the call is an ordinary failure" "$out" "FAIL abort_before: killed by SIGABRT"
expect_lacks "aborts: ... not an expected one" "$out" "expected an abort"
expect_contains "aborts: ... showing what it printed" "$out" "aborting early"

# A crash without the sanitizers, in a project of its own.
project "$work/nosan"
cat >tests/test_crash.smith <<'C'
import <signal.h>;
import <stdio.h>;
import <stdlib.h>;
import <unistd.h>;
import smith::test;

void test_abort_segv(void) {
    test::expect_abort();
    c::printf("before the crash\n");
    c::raise(c::SIGSEGV);
}

void test_abort_hangs(void) {
    test::expect_abort();
    c::printf("hanging\n");
    c::sleep(30);
}

void test_abort_exits_zero(void) {
    test::expect_abort();
    c::exit(0);
}

void test_abort_survivor(void) {
    test::expect_abort();
    c::abort();
}
C
capture "$smith" build -DSAN=0
expect "aborts: the unsanitized runner builds" 0 "$rc"
capture build/tests $iso --test abort_segv
expect_contains "aborts: a SIGSEGV fails" "$out" "FAIL abort_segv"
expect_contains "aborts: ... saying it crashed" "$out" "it crashed with SIGSEGV"
expect_contains "aborts: ... and showing what it printed first" "$out" "before the crash"
capture build/tests $iso --test abort_hangs --wall-timeout 1
expect_contains "aborts: a hang is the test's timeout" "$out" "FAIL abort_hangs: timed out after 1."
capture build/tests $iso --test abort_exits_zero
expect_contains "aborts: exit(0) is a return, not an abort" "$out" "expected an abort; the test returned"
capture build/tests $iso --test abort_ --wall-timeout 1
expect_contains "aborts: the tests after a crash, a hang and an exit still run" "$out" "1 passed, 3 failed"

# The three profiles. debug is the runners above (sanitizers on). release is -O2 with assertions on, and fast is -O2
# -DNDEBUG, where an assert is gone: abort() and __builtin_trap() still stop, and a test that expects an assert to stop it
# returns.
for profile in release fast; do
    project_c "$work/profile-$profile" # C only: the one project that checks the C API with nothing else around it
    if [ $profile = release ]; then flags='"-O2"'; else flags='"-O2", "-DNDEBUG"'; fi
    sed "s/\"-Wconversion\",/\"-Wconversion\", $flags,/" build.c >build.c.new && mv build.c.new build.c
    cat >tests/test_profile.c <<'C'
#include <smith/test.h>

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

void test_profile_abort(void) {
    smith_test_expect_abort();
    abort();
}

void test_profile_trap(void) {
    smith_test_expect_abort();
    __builtin_trap();
}

void test_profile_assert(void) {
    smith_test_expect_abort();
    assert(false);
}

void test_profile_returns(void) {
    smith_test_expect_abort();
}

void test_profile_before(void) {
    abort();
    smith_test_expect_abort();
}

void test_profile_segv(void) {
    smith_test_expect_abort();
    raise(SIGSEGV);
}
C
    capture "$smith" build -DSAN=0
    expect "profiles: the $profile runner builds" 0 "$rc"
    capture build/tests $iso --test profile_ --verbose
    expect_contains "profiles: $profile: abort() passes" "$out" "PASS profile_abort"
    expect_contains "profiles: $profile: __builtin_trap() passes" "$out" "PASS profile_trap"
    if [ $profile = release ]; then
        expect_contains "profiles: release: an assert passes" "$out" "PASS profile_assert"
        expect_contains "profiles: release: 3 passed, 3 failed" "$out" "3 passed, 3 failed"
    else
        expect_contains "profiles: fast: an assert is gone, so the test returns" "$out" "FAIL profile_assert"
        expect_contains "profiles: fast: 2 passed, 4 failed" "$out" "2 passed, 4 failed"
    fi
    expect_contains "profiles: $profile: returning fails" "$out" "FAIL profile_returns"
    expect_contains "profiles: $profile: ... saying so" "$out" "expected an abort; the test returned"
    expect_contains "profiles: $profile: an abort before the call is an ordinary failure" "$out" "FAIL profile_before: killed by SIGABRT"
    expect_contains "profiles: $profile: a SIGSEGV fails" "$out" "it crashed with SIGSEGV"
done
cd "$work/main" || exit 1

# 3. Isolation: a crash, a sanitizer report and a timeout each fail one test and the rest still run.
cat >tests/test_isolation.smith <<'C'
import <stdlib.h>;
import <string.h>;
import <unistd.h>;
import smith::test;

void test_overflow(void) {
    u8 *buffer = (u8 *)c::malloc(8);
    test::note("index %d", 8);
    buffer[8] = 'x';
    c::free(buffer);
}

void test_sleeps(void) {
    c::sleep(30);
}

void test_exits(void) {
    c::exit(3);
}

void test_spins(void) {
    volatile u64 count = 0;
    while (count < 0xffffffffffffffffULL) {
        count = count + 1;
    }
}

void test_naps(void) {
    c::usleep(2'500'000); // blocked, not busy: no CPU time, so it is inside a 1 s CPU limit
}

void test_alarm_hangs(void) {
    c::alarm(1);
    c::sleep(30);
}

void test_long_note(void) {
    char text[6000];
    c::memset(text, 'a', sizeof text - 1);
    text[sizeof text - 1] = '\0';
    test::note("%s", text);
    test::check_equal(1, 2);
}

void test_board_note(void) {
    test::note("board\n  . x .\n  x . x\n  . x .\nend");
    test::check_equal(1, 2);
}

void test_ok_after(void) {
    test::check_equal(1, 1);
}
C
capture "$smith" build
expect "isolation: the runner builds" 0 "$rc"
capture build/tests $iso --test overflow
expect_contains "isolation: an ASan report is shown" "$out" "AddressSanitizer: heap-buffer-overflow"
expect_contains "isolation: ... under the test's FAIL line" "$out" "FAIL overflow: killed by SIGABRT"
expect_contains "isolation: ... with the note" "$out" "    note: index 8"
capture build/tests $iso --test sleeps --wall-timeout 1
expect_contains "isolation: a timeout is reported" "$out" "FAIL sleeps: timed out after 1."
capture build/tests $iso --test spins --timeout 1 --wall-timeout 600
expect_contains "isolation: a busy test is over its CPU time limit" "$out" "FAIL spins: over its CPU time limit of 1 s ("
expect_contains "isolation: ... and says the wall time, since threads add CPU time" "$out" "s of wall time passed; threads add CPU time)"
# A worker stopped by a timer before it armed its clocks did not run the test (a left-over timer fired, SIGPROF with the
# test not started): the test gets a fresh worker, three times, and is never reported as over its CPU time limit.
if [ -z "$iso" ]; then
    capture env SMITH_TEST_KILL_WORKERS=2 build/tests --test ok_after --jobs 1 --verbose
    expect_contains "lost workers: a test whose worker was stopped twice before it began passes in the third" "$out" "PASS ok_after"
    capture env SMITH_TEST_KILL_WORKERS=20 build/tests --test ok_after --jobs 1
    expect_contains "lost workers: ... a test that never gets a worker is not reported as over its CPU time limit" "$out" "FAIL ok_after: the test did not run: its worker was stopped by SIGPROF 4 times before the test began"
    case $out in *"over its CPU time limit"*) expect "lost workers: ... and does not blame the test" "no blame" "$out" ;; *) expect "lost workers: ... and does not blame the test" "no blame" "no blame" ;; esac
fi
capture build/tests $iso --test naps --timeout 1 --verbose
expect_contains "isolation: a blocked test spends no CPU time, so the CPU limit does not end it" "$out" "PASS naps"
capture build/tests $iso --test naps --timeout 1 --wall-timeout 1
expect_contains "isolation: ... and the wall-clock backstop does" "$out" "FAIL naps: timed out after 1."
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
capture build/tests $iso --wall-timeout 1
expect_contains "isolation: the whole run reports every test" "$out" "failed in"
expect "isolation: ... and exits 1" 1 "$rc"
capture build/tests $iso --jobs 0
expect "options: a bad option exits 2" 2 "$rc"
expect_contains "options: ... and prints the usage" "$err" "usage:"
capture build/tests $iso --test nothing_like_this
expect "options: no matching test exits 1" "1 no test matches \"nothing_like_this\"" "$rc $out"

# 4. Running: --test filters, --seeds counts, --jobs 1 gives the same results.
cat >tests/test_properties.smith <<'C'
import smith::random;
import smith::test;

void test_prop_ok(random::Key key) {
    i64 x = random::uniform_int64_sample(key, (random::UniformInt64){.low = 0, .high = 1000});
    test::check_less_equal(x, 1000);
}

void test_prop_fails(random::Key key) {
    i64 x = random::uniform_int64_sample(key, (random::UniformInt64){.low = 0, .high = 1000});
    test::check_less(x, 500);
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
capture "$smith" test $iso --test prop_ok --seeds 3 -k
expect "task: -k is the build's (keep going), not the runner's" 0 "$rc"
capture "$smith" test $iso --test prop_ok --seeds 3 -k 2
expect "task: -k N too" 0 "$rc"
capture "$smith" test $iso --test prop_ok --seeds x
expect "task: a bad option reaches the runner's usage, and fails the task" yes "$([ "$rc" -ne 0 ] && echo yes)"
expect_contains "task: ... which says so" "$err$out" "usage:"
capture "$smith" test $iso --test ok_after --jobs 1
expect "task: --test filters, and a passing run exits 0" 0 "$rc"
capture "$smith" test $iso --test stops
expect_contains "task: a failing test is printed" "$out$err" "FAIL stops"
expect "task: ... and the task fails" 1 "$rc"
all=$(build/tests $iso --test abort 2>&1 | plain | grep -v 'smith-trace \(image\|frame\)') # the addresses differ per run
one=$(build/tests $iso --test abort --jobs 1 2>&1 | plain | grep -v 'smith-trace \(image\|frame\)')
expect "jobs: --jobs 1 gives the same results" "$all" "$one"

capture build/tests $iso --test prop_fails --shrink 0
seed=$(printf '%s' "$out" | sed -n 's/^FAIL prop_fails with seed \([0-9]*\).*/\1/p')
expect "properties: a failure names its seed" yes "$([ -n "$seed" ] && echo yes)"
expect_contains "properties: ... and a rerun line" "$out" "    rerun: smith test --test prop_fails --seed $seed"
# The task prints the values under the failing line, named by their text (x = 700, 500); the runner on its own prints them as a
# and b. The seed, the first value and the rerun line are the same either way.
digest() { # the header, the first value and the rerun line of the failure in the text on standard input
    sed -n '/^FAIL prop_fails/,/rerun/p' | grep -E '^FAIL|^ +[ax] = |rerun' | sed -e 's/^ *//' -e 's/^[ax] = \([0-9]*\).*/first \1/'
}
first=$(printf '%s' "$out" | digest)
capture "$smith" test $iso --test prop_fails --seed "$seed" --shrink 0
second=$(printf '%s%s' "$out" "$err" | digest)
expect "properties: the rerun line reproduces the failure" "$first" "$second"
capture build/tests $iso --test prop_fails --seed "$seed" --shrink 0
third=$(printf '%s' "$out" | digest)
expect "properties: --seed reproduces it too" "$first" "$third"

# 4a. The checks as generic functions: the ordering checks (a NaN is unordered, so every comparison but not_equal fails on
# one), check_not_equal, check_near and check_same_bits for f32 and f64, a pointer against nullptr, a type written first,
# arguments that are expressions, and test::memory.
project "$work/api"
cat >tests/test_api.smith <<'C'
import smith::arena;
import smith::math;
import smith::test;

void test_ordering_passes(void) {
    i32 one = 1;
    i32 two = 2;
    test::check_less(one, two);
    test::check_less_equal(one, two);
    test::check_less_equal(two, 2);
    test::check_greater(two, one);
    test::check_greater_equal(two, one);
    test::check_greater_equal(two, 2);
    test::check_not_equal(one, two);
    u8 small = 3;
    u8 big = 200;
    test::check_less(small, big);
    f64 pi = 3.14159;
    test::check_greater(pi, 3.0);
    test::check_less(-1.5, pi);
    char a = 'a';
    test::check_less(a, 'b');
    bool no = false;
    test::check_not_equal(no, true);
}

void test_lt_fails(void) {
    i32 a = 5;
    i32 b = 5;
    test::check_less(a, b);
}

void test_le_fails(void) {
    u16 a = 6;
    u16 b = 5;
    test::check_less_equal(a, b);
}

void test_gt_fails(void) {
    i64 a = -5;
    i64 b = 5;
    test::check_greater(a, b);
}

void test_ge_fails(void) {
    i8 a = -5;
    i8 b = 5;
    test::check_greater_equal(a, b);
}

void test_ne_fails(void) {
    i32 count = 5;
    test::check_not_equal(count, 5);
}

void test_nan_lt(void) {
    f64 not_a_number = math::nan(f64);
    test::check_less(not_a_number, 1.0);
}

void test_nan_le(void) {
    f64 not_a_number = math::nan(f64);
    test::check_less_equal(1.0, not_a_number);
}

void test_nan_gt(void) {
    f32 not_a_number = math::nan(f32);
    test::check_greater(not_a_number, 1.0f);
}

void test_nan_ge(void) {
    f32 not_a_number = math::nan(f32);
    test::check_greater_equal(not_a_number, not_a_number);
}

void test_nan_eq(void) {
    f64 not_a_number = math::nan(f64);
    test::check_equal(not_a_number, not_a_number);
}

void test_nan_ne_passes(void) {
    f64 not_a_number = math::nan(f64);
    test::check_not_equal(not_a_number, not_a_number);
    test::check_not_equal(not_a_number, 1.0);
}

void test_near_generic_passes(void) {
    f32 a = 1.0f;
    f32 b = 1.05f;
    test::check_near(a, b, 0.1f);
    f64 x = 1.0;
    f64 y = 1.0 + 1e-12;
    test::check_near(x, y, 1e-9);
    f64 infinity = math::infinity(f64);
    test::check_near(infinity, infinity, 0.0);
}

void test_near_f32_fails(void) {
    f32 a = 1.0f;
    f32 b = 1.5f;
    f32 tolerance = 0.1f;
    test::check_near(a, b, tolerance);
}

void test_near_infinity_fails(void) {
    f64 infinity = math::infinity(f64);
    f64 big = 1e300;
    test::check_near(infinity, big, big);
}

void test_same_bits_passes(void) {
    f64 not_a_number = math::nan(f64);
    test::check_same_bits(not_a_number, not_a_number);
    f32 minus_zero = -0.0f;
    test::check_same_bits(minus_zero, -0.0f);
}

void test_same_bits_zero_fails(void) {
    f64 zero = 0.0;
    test::check_same_bits(zero, -0.0);
}

void test_same_bits_f32_fails(void) {
    f32 one = 1.0f;
    f32 next = 1.0000001f;
    test::check_same_bits(one, next);
}

void test_pointer_passes(void) {
    i32 value = 1;
    i32 *none = nullptr;
    i32 *some = &value;
    test::check_equal(none, nullptr);
    test::check_not_equal(some, nullptr);
    test::check_equal(some, &value);
    i32 values[2] = {1, 2};
    test::check_less(&values[0], &values[1]);
}

void test_pointer_null_fails(void) {
    i32 value = 1;
    i32 *some = &value;
    test::check_equal(some, nullptr);
}

void test_pointer_not_null_fails(void) {
    i32 *none = nullptr;
    test::check_not_equal(none, nullptr);
}

void test_typed_passes(void) {
    u32 small = 3;
    u64 big = 3;
    test::check_equal((u64)small, big);
    test::check_less(small, 10);
}

void test_expressions_pass(void) {
    i32 a = 2;
    test::check_equal(a * 2 + 1, a > 1 ? 5 : 6);
    test::check_less(a - 1, a);
    test::check_equal(a + 1, 3);
}

void test_memory_passes(void) {
    arena::Arena memory = test::memory(1 << 12);
    i32 *items = arena::push_array(&memory, i32, 10);
    for (i32 i = 0; i < 10; i++) {
        items[i] = i * i;
    }
    test::check_equal(items[9], 81);
    f64 *halves = arena::push_array(&memory, f64, 4);
    halves[3] = 1.5;
    test::check_near(halves[3], 1.5, 0.0);
}

void test_memory_stops(void) {
    arena::Arena memory = test::memory(256);
    test::expect_abort();
    (void)arena::push_array(&memory, f64, 100);
}
C
capture "$smith" build
expect "api: the runner builds" 0 "$rc"
if [ "$rc" -ne 0 ]; then printf '%s\n%s\n' "$out" "$err"; fi
capture build/tests $iso --list
expect "api: the tests are found" "ordering_passes
lt_fails
le_fails
gt_fails
ge_fails
ne_fails
nan_lt
nan_le
nan_gt
nan_ge
nan_eq
nan_ne_passes
near_generic_passes
near_f32_fails
near_infinity_fails
same_bits_passes
same_bits_zero_fails
same_bits_f32_fails
pointer_passes
pointer_null_fails
pointer_not_null_fails
typed_passes
expressions_pass
memory_passes
memory_stops" "$out"
capture build/tests $iso --verbose --test _pass
expect_contains "api: the ordering checks pass on ordered values of every type" "$out" "PASS ordering_passes"
expect_contains "api: ... not_equal passes on a NaN, which equals nothing" "$out" "PASS nan_ne_passes"
expect_contains "api: ... near passes for f32 and f64 and for equal infinities" "$out" "PASS near_generic_passes"
expect_contains "api: ... same_bits passes for a NaN and -0 against itself" "$out" "PASS same_bits_passes"
expect_contains "api: ... a pointer is checked against nullptr and its own address" "$out" "PASS pointer_passes"
expect_contains "api: ... a cast on the first argument gives the type" "$out" "PASS typed_passes"
expect_contains "api: ... arguments may be expressions" "$out" "PASS expressions_pass"
expect_contains "api: ... test::memory gives a typed arena" "$out" "PASS memory_passes"
expect_contains "api: ... and none of them failed" "$out" "8 passed, 0 failed"
capture build/tests $iso --verbose --test memory_stops
expect_contains "api: the arena of test::memory stops on a push past its capacity" "$out" "PASS memory_stops"

if [ -n "$have_symbolizer" ]; then
    expect_fail() { # name, test, source text, the values line
        line=$(line_of tests/test_api.smith "$3")
        expect_report "$1" "FAIL $2
  check failed at tests/test_api.smith:$line
     $line |     $3
           $4
  called from
    tests/test_api.smith:$line test_$2

0 passed, 1 failed" --test "$2"
    }
    expect_fail "api: check_less fails on equal values" lt_fails 'test::check_less(a, b);' "a = 5, b = 5   (i32)"
    expect_fail "api: check_less_equal fails on a greater value, and prints a u16" le_fails 'test::check_less_equal(a, b);' "a = 6, b = 5   (u16)"
    expect_fail "api: check_greater fails on a smaller value, and prints an i64" gt_fails 'test::check_greater(a, b);' "a = -5, b = 5   (i64)"
    expect_fail "api: check_greater_equal fails on a smaller value, and prints an i8" ge_fails 'test::check_greater_equal(a, b);' "a = -5, b = 5   (i8)"
    expect_fail "api: check_not_equal fails on equal values" ne_fails 'test::check_not_equal(count, 5);' "count = 5, 5   (i32)"
    expect_fail "api: a NaN is not less than a number" nan_lt 'test::check_less(not_a_number, 1.0);' "not_a_number = nan, 1.0 = 1 [0x1p+0]   (f64)"
    expect_fail "api: ... nor is a number less than or equal to it" nan_le 'test::check_less_equal(1.0, not_a_number);' "1.0 = 1 [0x1p+0], not_a_number = nan   (f64)"
    expect_fail "api: ... nor greater, in f32" nan_gt 'test::check_greater(not_a_number, 1.0f);' "not_a_number = nan, 1.0f = 1 [0x1p+0]   (f32)"
    expect_fail "api: ... a NaN is not greater than or equal to itself" nan_ge 'test::check_greater_equal(not_a_number, not_a_number);' "not_a_number = nan, not_a_number = nan   (f32)"
    expect_fail "api: ... and not equal to itself" nan_eq 'test::check_equal(not_a_number, not_a_number);' "not_a_number = nan, not_a_number = nan   (f64)"
    expect_fail "api: check_same_bits tells 0 from -0, in hex" same_bits_zero_fails 'test::check_same_bits(zero, -0.0);' "zero = 0 [0x0p+0], -0.0 = -0 [-0x0p+0]   (f64)"
    expect_fail "api: ... and f32 values one bit apart" same_bits_f32_fails 'test::check_same_bits(one, next);' "one = 1 [0x1p+0], next = 1.0000001 [0x1.000002p+0]   (f32)"

    line=$(line_of tests/test_api.smith 'test::check_near(a, b, tolerance);')
    expect_report "api: check_near for f32 shows the values, the tolerance and the distance" "FAIL near_f32_fails
  check failed at tests/test_api.smith:$line
     $line |     test::check_near(a, b, tolerance);
           a = 1 [0x1p+0]   (f32)
           b = 1.5 [0x1.8p+0]   (f32)
           tolerance = 0.1 [0x1.99999ap-4]   (f32)
           distance = 0.5 [0x1p-1]   (f32)
  called from
    tests/test_api.smith:$line test_near_f32_fails

0 passed, 1 failed" --test near_f32_fails
    capture sh -c "build/tests $iso --test near_infinity_fails --trace 2>&1 | '$smith' symbolize"
    expect_contains "api: check_near fails on an infinity against a finite value" "$out" "check failed at tests/test_api.smith:"
    expect_contains "api: ... and the distance is inf" "$out" "= inf   (f64)"

    capture sh -c "build/tests $iso --test pointer_null_fails --trace 2>&1 | '$smith' symbolize"
    expect_contains "api: a non-null pointer against nullptr prints its address and nullptr" "$out" "some = 0x"
    expect_contains "api: ... and the type" "$out" ", nullptr   (ptr)"
    expect_report "api: a null pointer fails check_not_equal against nullptr" "FAIL pointer_not_null_fails
  check failed at tests/test_api.smith:$(line_of tests/test_api.smith 'test::check_not_equal(none, nullptr);')
     $(line_of tests/test_api.smith 'test::check_not_equal(none, nullptr);') |     test::check_not_equal(none, nullptr);
           none = nullptr, nullptr   (ptr)
  called from
    tests/test_api.smith:$(line_of tests/test_api.smith 'test::check_not_equal(none, nullptr);') test_pointer_not_null_fails

0 passed, 1 failed" --test pointer_not_null_fails
fi

# A second argument that does not keep its value in the first's type is a compile error that names the cast.
project "$work/conflict"
cat >tests/test_conflict.smith <<'C'
import smith::test;

void test_conflict(void) {
    u32 small = 3;
    u64 big = 3;
    test::check_equal(small, big);
}
C
capture "$smith" build
expect "conflict: two types in one check fail to build" 1 "$rc"
expect_contains "conflict: ... naming the type and the cast" "$out$err" "converts to u32, the type of its first argument \`small\`"
fi

# 4b. Shrinking, in three parts (each runs the same test file in a project of its own).
if part shrink_int shrink_list shrink_normal; then
project "$work/shrink"
cat >tests/test_shrink.smith <<'C'
import smith::random;
import smith::test;

void test_shrink_int(random::Key key) {
    i64 x = random::uniform_int64_sample(key, (random::UniformInt64){.low = 0, .high = 1000});
    test::check_less(x, 500);
}

void test_shrink_list(random::Key key) {
    i64 length = random::uniform_int64_sample(random::key_derive(key, 0), (random::UniformInt64){.low = 0, .high = 8});
    for (i64 i = 0; i < length; i++) {
        i64 item = random::uniform_int64_sample(random::key_derive(key, (u64)i + 1), (random::UniformInt64){.low = 0, .high = 100});
        test::note("length %lld, item %lld", length, i);
        test::check_less_equal(item, 90);
    }
}

void test_shrink_normal(random::Key key) {
    f64 z = random::normal_double_sample(key, (random::NormalDouble){.mean = 0, .deviation = 1});
    test::check_less(z, 3.0);
}

void test_shrink_moves(random::Key key) {
    i64 x = random::uniform_int64_sample(key, (random::UniformInt64){.low = 0, .high = 1000});
    test::check_less(x, 500);
    test::check_not_equal(x, 0); // the failure that shrinking to 0 would move to
}
C
capture "$smith" build
expect "shrink: the runner builds" 0 "$rc"
fi

if part shrink_int; then
line=$(line_of tests/test_shrink.smith 'test::check_less(x, 500);')
shrunk=$(report --test shrink_int --shrink-seconds 3600)
seed=$(printf '%s' "$shrunk" | sed -n 's/^FAIL shrink_int with seed \([0-9]*\),.*/\1/p')
runs=$(printf '%s' "$shrunk" | sed -n 's/.*shrunk in \([0-9]*\) runs.*/\1/p')
if [ -n "$have_symbolizer" ]; then
    expect "shrink: a uniform [0, 1000] with x < 500 shrinks to 500" "FAIL shrink_int with seed $seed, shrunk in RUNS runs
 check failed at tests/test_shrink.smith:$line
 $line | test::check_less(x, 500);
 x = 500, 500 (i64)
 called from
 tests/test_shrink.smith:$line test_shrink_int
 rerun: smith test --test shrink_int --seed $seed

0 passed, 1 failed" "$(printf '%s' "$shrunk" | sed 's/shrunk in [0-9]* runs/shrunk in RUNS runs/' | tr -s ' ')"
fi
capture build/tests $iso --test shrink_int --shrink-seconds 3600
expect_contains "shrink: the runner on its own shows 500 and 500" "$out" "    a = 500   (i64)
    b = 500   (i64)"
expect "shrink: ... in at most 1000 runs" yes "$([ -n "$runs" ] && [ "$runs" -le 1000 ] && echo yes)"

capture build/tests $iso --test shrink_int --shrink 0
unshrunk=$(printf '%s' "$out" | sed -n 's/^    a = \([0-9]*\) .*/\1/p')
expect "shrink: --shrink 0 shows the unshrunk failure" yes "$([ "$unshrunk" -gt 500 ] && echo yes)"
expect_lacks "shrink: ... without 'shrunk'" "$out" "shrunk"

capture "$smith" test $iso --test shrink_int --seed "$seed"
expect_contains "shrink: the rerun line shrinks to the same case" "$out$err" "    x = 500, 500   (i64)"
fi

if part shrink_list; then
# The same command twice, side by side: it takes a minute under --isolate. The addresses of the trace differ per run.
(build/tests $iso --test shrink_list --shrink-seconds 3600 2>&1 | plain | grep -v 'smith-trace \(image\|frame\)' >"$work/shrink-first") &
(build/tests $iso --test shrink_list --shrink-seconds 3600 2>&1 | plain | grep -v 'smith-trace \(image\|frame\)' >"$work/shrink-second") &
wait
first=$(cat "$work/shrink-first")
expect_contains "shrink: a list shrinks to one item" "$first" "    note: length 1, item 0"
expect_contains "shrink: ... of 91" "$first" "    a = 91   (i64)"
second=$(cat "$work/shrink-second")
expect "shrink: the same seed gives the same shrunk case" "$first" "$second"
fi

if part shrink_normal; then
capture build/tests $iso --test shrink_normal --shrink-seconds 3600 --seeds 5000
z=$(printf '%s' "$out" | sed -n 's/^    a = \([0-9.e+-]*\) .*/\1/p')
expect_contains "shrink: a normal with z < 3 shrinks" "$out" "FAIL shrink_normal with seed"
expect "shrink: ... to the z nearest 3 from above" yes "$(awk -v z="$z" 'BEGIN { print (z >= 3 && z < 3.0000001) ? "yes" : "no" }')"
fi

if part shrink_int; then
capture build/tests $iso --test shrink_moves --shrink-seconds 3600
expect_contains "shrink: a failure that moves to another check is not kept: x is not 0" "$out" "    a = 500   (i64)"
if [ -n "$have_symbolizer" ]; then
    line=$(grep -n -F 'test::check_less(x, 500);' tests/test_shrink.smith | sed -n 2p | cut -d: -f1)
    capture sh -c "build/tests $iso --test shrink_moves --shrink-seconds 3600 --trace 2>&1 | '$smith' symbolize"
    expect_contains "shrink: ... and it is the first check" "$out" "  check failed at tests/test_shrink.smith:$line"
fi

fi

if part alongside; then
# Shrinking does not make another test depend on it: while one test shrinks for seconds, a healthy test that writes more
# than 64 KiB, waits a second and writes more, still passes with --timeout 2.
project "$work/alongside"
cat >tests/test_alongside.smith <<'C'
import <stdio.h>;
import <unistd.h>;
import smith::random;
import smith::test;

void test_a_shrinks_slowly(random::Key key) {
    c::usleep(3000); // each of the 1000 runs of shrinking takes this long: several seconds in all
    i64 smallest = 1000000000;
    for (u64 i = 0; i < 200; i++) {
        i64 v = random::uniform_int64_sample(random::key_derive(key, i), (random::UniformInt64){.low = 0, .high = 1000000000});
        if (v < smallest) {
            smallest = v;
        }
    }
    test::check_less(smallest, 1000);
}

void test_b_writes_a_lot(void) {
    for (i32 i = 0; i < 1000; i++) {
        c::printf("%0100d\n", i); // 100 KB, past the 64 KiB the runner keeps
    }
    c::usleep(1000000); // while the runner shrinks the other test
    for (i32 i = 0; i < 3000; i++) {
        c::printf("%0100d\n", i); // 300 KB more: it blocks if nobody reads the pipe
    }
    c::printf("done\n");
}
C
capture "$smith" build -DSAN=0
expect "alongside: the runner builds" 0 "$rc"
capture build/tests $iso --timeout 2 --jobs 2
expect_contains "alongside: the shrinking test fails and is shrunk" "$out" "FAIL a_shrinks_slowly with seed"
expect_lacks "alongside: ... and the other test is not a failure" "$out" "FAIL b_writes_a_lot"
expect_contains "alongside: ... it passes" "$out" "1 passed, 1 failed"
fi

if part budget; then
# The budget: a test that needs far more than 1000 runs to shrink stops at 1000 runs or 10 seconds.
project "$work/budget"
cat >tests/test_budget.smith <<'C'
import smith::random;
import smith::test;

void test_budget(random::Key key) {
    i64 smallest = 1000000000;
    for (u64 i = 0; i < 200; i++) {
        i64 v = random::uniform_int64_sample(random::key_derive(key, i), (random::UniformInt64){.low = 0, .high = 1000000000});
        if (v < smallest) {
            smallest = v;
        }
    }
    test::check_less(smallest, 1000);
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
fi

if part misc; then
# 5. SIGSEGV without the sanitizers is named.
project "$work/plain"
cat >tests/test_crash.smith <<'C'
import smith::test;

void test_crash(void) {
    volatile i32 *pointer = nullptr;
    test::note("about to crash");
    *pointer = 1;
}

void test_survivor(void) {
    test::check(true);
}
C
capture "$smith" build -DSAN=0
expect "crash: the runner builds without sanitizers" 0 "$rc"
capture build/tests $iso
expect_contains "crash: a SIGSEGV is named" "$out" "FAIL crash: killed by SIGSEGV"
expect_contains "crash: ... with the note" "$out" "    note: about to crash"
expect_contains "crash: ... and the other test passed" "$out" "1 passed, 1 failed"

# 6. Finding the tests: in .smith and in C files, side by side.
project "$work/finding"
cat >tests/test_a.smith <<'C'
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
expect_contains "duplicate: ... naming both files, a .smith one and a C one" "$out$err" "smith: smith_tests: test_same is defined in tests/test_a.smith and in tests/test_b.c"
rm tests/test_b.c
cat >tests/test_b.smith <<'C'
void test_odd (void) {
}
C
capture "$smith" build
expect "formatting: a test the scan missed fails to compile" 1 "$rc"
expect_contains "formatting: ... with the missing prototype" "$out$err" "no previous prototype for function 'test_odd'"
cat >tests/test_b.smith <<'C'
[[maybe_unused]] static i32 helper(i32 value) {
    return value + 1;
}

[[maybe_unused]] static i32 not_a_test(i32 value) {
    return helper(value);
}
C
capture "$smith" build
expect "no tests: a file without tests is fine" 0 "$rc"
capture build/tests $iso --list
expect "no tests: ... and adds nothing" "same" "$out"
rm tests/test_a.smith
capture "$smith" build
expect "no tests: a project with none builds" 0 "$rc"
capture build/tests $iso
expect_contains "no tests: ... and runs nothing" "$out" "0 passed, 0 failed"
expect "no tests: ... with status 0" 0 "$rc"

# 7. Conversions: a lossy comparison in C is a -Wconversion error (the C names take the type in their name). In .smith the
# types of the two arguments must agree, which is checked in section 4a.
project "$work/lossy"
cat >tests/test_lossy.c <<'C'
#include <smith/test.h>

void test_lossy(void) {
    int count = 3;
    smith_check_equal_i32(count, 3.5);
}
C
capture "$smith" build
expect "conversion: a lossy comparison fails to compile" 1 "$rc"
expect_contains "conversion: ... with -Wconversion's message" "$out$err" "implicit conversion from 'double' to"

# 8. smith_tests links the arena and random itself when .objects leaves them out, and builds with no .objects at all.
project "$work/missing"
cat >tests/test_x.smith <<'C'
import smith::test;

void test_x(void) {
    test::check(true);
}
C
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu2y", "-I", ".smith/include", nullptr);
    SmithObject arena = {.compiler = "smith-cc", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
    smith_object(b, &arena);
    SmithTests tests = {
        .sources = smith_list(b, "tests/test_x.smith", nullptr),
        .objects = (const SmithObject *[]){&arena, nullptr},
        .flags = flags,
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
C
capture "$smith" build
expect "objects: a library left out of .objects is linked by smith_tests" 0 "$rc"
capture build/tests
expect_contains "objects: ... and the test runs" "$out" "1 passed, 0 failed"
sed -i.bak -e 's/    smith_object(b, &arena);//' -e 's/    SmithObject arena = .*//' -e '/\.objects = /d' build.c
capture "$smith" build
expect "objects: no .objects at all builds" 0 "$rc"
capture build/tests
expect_contains "objects: ... and the test runs without them" "$out" "1 passed, 0 failed"

# 8b. Full size, check_same_bits and .inputs.
project "$work/extras"
cat >tests/test_extras.smith <<'C'
import smith::math;
import smith::test;

void test_size(void) {
    usize count = test::full() ? 1000 : 10;
    test::note("count %zu", count);
    test::check_equal(count, 10);
}

void test_bits_equal(void) {
    test::check_same_bits(0.1 + 0.2, 0.30000000000000004);
    test::check_same_bits(-0.0, -0.0);
    test::check_same_bits(1.5f, 1.5f);
    f64 not_a_number = math::nan(f64);
    test::check_same_bits(not_a_number, not_a_number);
    f64 minus_infinity = -math::infinity(f64);
    test::check_same_bits(minus_infinity, minus_infinity);
}

void test_bits_zero(void) {
    f64 zero = 0.0;
    test::check_same_bits(zero, -0.0);
}

void test_bits_nan(void) {
    f64 first = __builtin_nan("1");
    f64 second = __builtin_nan("2");
    test::check_same_bits(first, second);
}

void test_bits_float(void) {
    f32 one = 1.0f;
    f32 next = 1.0000001f;
    test::check_same_bits(one, next);
}
C
cat >tests/test_extras_c.c <<'C'
#include <smith/test.h>

#include <stdio.h>
#include <string.h>

void test_reads_input(void) {
    FILE *file = fopen("build/data.txt", "r");
    smith_check(file != nullptr);
    char text[16] = "";
    smith_check(fgets(text, sizeof text, file) != nullptr);
    fclose(file);
    smith_test_note("data %s", text);
    smith_check_equal_i32(strcmp(text, "one\n"), 0);
}
C
cat >gen.sh <<'S'
echo one >"$1"
S
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu2y", "-g", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-I", ".smith/include", nullptr);
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
        .sources = smith_list(b, "tests/test_extras.smith", "tests/test_extras_c.c", nullptr),
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
if [ "$rc" -ne 0 ]; then printf '%s\n%s\n' "$out" "$err"; fi
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
if [ -n "$have_symbolizer" ]; then
    line=$(line_of tests/test_extras.smith 'test::check_same_bits(zero, -0.0);')
    expect_report "bits: 0.0 against -0.0 fails, with both in hex" "FAIL bits_zero
  check failed at tests/test_extras.smith:$line
     $line |     test::check_same_bits(zero, -0.0);
           zero = 0 [0x0p+0], -0.0 = -0 [-0x0p+0]   (f64)
  called from
    tests/test_extras.smith:$line test_bits_zero

0 passed, 1 failed" --test bits_zero
fi
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
expect_contains "inputs: ... as the note shows" "$out$err" "note: data two"

fi

if part workers; then
# 8c. Workers: tests run one after another in a pool of long-lived processes, and --isolate gives each its own. --stats
# prints the forks the run made, which tells the two apart. A group is chosen with --test <prefix> and runs on one worker
# (--jobs 1); its tests run in the order written.
project "$work/workers"
cat >tests/test_workers.smith <<'C'
import <assert.h>;
import <stdio.h>;
import <stdlib.h>;
import <unistd.h>;
import smith::arena;
import smith::random;
import smith::test;

void test_crash_1_before(void) {
    test::check_equal(1, 1);
}

void test_crash_2_dies(void) {
    test::note("about to abort");
    c::abort();
}

void test_crash_3_after(void) {
    test::check_equal(2, 2);
}

void test_crash_4_after(void) {
    test::check_equal(3, 3);
}

void test_late_1_before(void) {
    test::check(true);
}

void test_late_2_sleeps(void) {
    c::sleep(30);
}

void test_late_3_after(void) {
    test::check(true);
}

void test_report_1_before(void) {
    test::check(true);
}

void test_report_2_overflows(void) {
    u8 *bytes = (u8 *)c::malloc(8);
    volatile usize index = 8;
    bytes[index] = 'x';
    c::free(bytes);
}

void test_report_3_prints_one(void) {
    c::printf("AddressSanitizer: a test that only says so\n");
}

void test_report_4_after(void) {
    test::check(true);
}

void test_aborts_1_assert(void) {
    test::expect_abort();
    c::assert(false);
}

void test_aborts_2_quiet(void) {
    test::expect_abort();
    c::printf("hidden-by-the-check\n");
    c::abort();
}

void test_aborts_3_reads(void) {
    u8 *bytes = (u8 *)c::malloc(8);
    volatile usize index = 8;
    test::expect_abort();
    c::printf("before the read\n");
    (void)bytes[index];
    c::free(bytes);
}

void test_aborts_4_after(void) {
    test::expect_abort();
    c::abort();
}

void test_unwind_1_fails(void) {
    test::check_equal(1, 2);
}

void test_unwind_2_fails(void) {
    test::check_equal(3, 4);
}

void test_unwind_3_passes(void) {
    test::check(true);
}

void test_out_1_fails(void) {
    c::printf("from-1\n");
    c::fprintf(c::stderr, "err-from-1\n");
    test::check_equal(1, 2);
}

void test_out_2_passes(void) {
    c::printf("from-2\n");
    c::fprintf(c::stderr, "err-from-2\n");
}

void test_out_3_fails(void) {
    c::printf("from-3\n");
    test::check_equal(1, 2);
}

void test_out_4_buffers(void) {
    static char buffer[256];
    c::setvbuf(c::stdout, buffer, c::_IOFBF, sizeof buffer);
    c::printf("from-4\n"); // still in the test's own buffer when the test returns
}

void test_out_5_fails(void) {
    test::check_equal(1, 2);
}

static i32 dirty_death;
static i32 dirty_failed;

void test_dirty_1_death(void) {
    dirty_death = 1;
    test::expect_abort(); // stands for the program ending: the worker ends with it
    c::abort();
}

void test_dirty_2_fresh(void) {
    test::check_equal(dirty_death, 0);
}

void test_dirty_3_fails(void) {
    dirty_failed = 1;
    test::check_equal(1, 2); // unwinds out of whatever it was doing: the worker ends
}

void test_dirty_4_fresh(void) {
    test::check_equal(dirty_failed, 0);
}

// A failure that depends on state: the first run in a process fails only for a large x, a later one always fails. Shrinking
// reruns the key in one worker, so its smaller cases fail on the state its earlier attempts left, not on their own.
static i32 leaky_runs;

void test_shrink_leaky(random::Key key) {
    i64 x = random::uniform_int64_sample(key, (random::UniformInt64){.low = 0, .high = 1000});
    leaky_runs += 1;
    bool fails = leaky_runs == 1 ? x >= 500 : true;
    test::check_equal(fails, false);
}

static void *fresh_base;

void test_fresh_1_sets(void) {
    test::note("note from 1");
    arena::Arena memory = test::memory(4096);
    u8 *bytes = arena::push_array(&memory, u8, 100);
    fresh_base = bytes;
    for (usize i = 0; i < 100; i++) {
        bytes[i] = 0xAB;
    }
}

void test_fresh_2_sees(void) {
    arena::Arena memory = test::memory(4096);
    u8 *bytes = arena::push_array(&memory, u8, 100);
    if (fresh_base != nullptr) {
        test::check_equal((void *)bytes, fresh_base); // given back: the same memory
    }
    for (usize i = 0; i < 100; i++) {
        test::check_equal(bytes[i], 0);
    }
    test::check_equal(1, 2); // fails to show the note it was given: none
}

void test_fresh_3_big(void) {
    arena::Arena memory = test::memory(200U << 20);
    u64 *item = arena::push(&memory, u64);
    test::check_not_equal(item, nullptr);
}

void test_fresh_4_big(void) {
    arena::Arena memory = test::memory(200U << 20); // out of capacity if the last test's was not given back
    u64 *item = arena::push(&memory, u64);
    test::check_not_equal(item, nullptr);
}

static i32 ran;

void test_order_a(void) {
    ran = 1;
}

void test_order_b(void) {
    test::check_equal(ran, 1); // only if order_a ran first, in this process
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

capture build/tests $iso --test late_ --jobs 1 --wall-timeout 1 --stats
expect_contains "workers: a timeout fails its test" "$out" "FAIL late_2_sleeps: timed out after 1."
expect_contains "workers: ... and the tests around it pass" "$out" "2 passed, 1 failed"
expect "workers: ... it replaced the worker" "$(by_mode 2 3)" "$(forks_of "$out")"

capture build/tests $iso --test report_ --jobs 1 --stats
expect_contains "workers: a sanitizer report fails its test" "$out" "FAIL report_2_overflows: killed by SIGABRT"
expect_contains "workers: ... with the report" "$out" "AddressSanitizer: heap-buffer-overflow"
expect_contains "workers: ... and a test that only prints a report passes" "$out" "3 passed, 1 failed"
expect "workers: ... the crash and the printed report each replaced the worker" "$(by_mode 3 4)" "$(forks_of "$out")"

capture build/tests $iso --test aborts_ --jobs 1 --stats --verbose
expect_contains "workers: an expected abort passes" "$out" "PASS aborts_1_assert"
expect_contains "workers: ... and another" "$out" "PASS aborts_2_quiet"
expect_lacks "workers: ... hiding the test's output" "$out" "hidden-by-the-check"
expect_contains "workers: ... a sanitizer report fails the test" "$out" "FAIL aborts_3_reads"
expect_contains "workers: ... saying so" "$out" "expected an abort; it was stopped by a sanitizer report"
expect_contains "workers: ... with the report" "$out" "AddressSanitizer: heap-buffer-overflow"
expect_contains "workers: ... and shows what the test printed" "$out" "before the read"
expect_contains "workers: ... a later check passes" "$out" "PASS aborts_4_after"
expect "workers: a test that expected an abort ends its worker, so one fork a test" 4 "$(forks_of "$out")"

capture build/tests $iso --test aborts_ --jobs 4
expect_contains "workers: ... with several jobs, the sanitizer report still fails its test" "$out" "FAIL aborts_3_reads"
expect_contains "workers: ... and the other three pass" "$out" "3 passed, 1 failed"

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
expect_contains "dirty: a test after an expected abort sees clean globals, and after a failed test too" "$out" "3 passed, 1 failed"
expect_contains "dirty: ... only the failing test failed" "$out" "FAIL dirty_3_fails"
expect "dirty: ... the expected abort and the failure each ended a worker" "$(by_mode 3 4)" "$(forks_of "$out")"

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
    first=$(build/tests --test order_ --jobs 1 --shuffle "${found:-1}" --verbose 2>&1 | plain | grep -v 'smith-trace \(image\|frame\)')
    second=$(build/tests --test order_ --jobs 1 --shuffle "${found:-1}" --verbose 2>&1 | plain | grep -v 'smith-trace \(image\|frame\)')
    expect "shuffle: the same seed gives the same results (the trace's addresses differ per run)" "$first" "$second"
    expect_contains "shuffle: ... reported in table order" "$first" "PASS order_a
FAIL order_b"
    capture "$smith" test --test order_ --jobs 1 --shuffle "${found:-1}"
    expect_contains "shuffle: --shuffle reaches the runner" "$out$err" "FAIL order_b"
fi

# Many jobs under a low descriptor limit: every job's worker holds two descriptors in the runner, which raises the limit.
project "$work/many"
{
    echo 'import <unistd.h>;'
    for number in $(seq 1 90); do
        printf 'void test_many_%s(void) {\n    c::usleep(200000);\n}\n' "$number"
    done
} >tests/test_many.smith
capture "$smith" build -DSAN=0
expect "many: the runner builds" 0 "$rc"
capture sh -c "ulimit -S -n 100; build/tests $iso --jobs 90"
expect_contains "many: 90 jobs under a limit of 100 descriptors all run" "$out" "90 passed, 0 failed"
fi

# 9. Tools: clangd reads a test file without errors.
if part main; then
cd "$work/main" || exit 1
if "$smith" lsp --version >/dev/null 2>&1; then
    capture "$smith" build
    # The sample is not what smith-effects tests; its checks are off.
    printf 'Checks: "-*"\n' >.clang-tidy
    capture "$smith" lsp --check=tests/test_checks.smith
    errors=$(printf '%s\n%s\n' "$out" "$err" | grep -c 'E\[')
    expect "clangd: no errors in a test file" 0 "$errors"
fi
# The analyzer: test::check(p != nullptr) is an assumption to it, as contract::assert is (target::static_analyzer), so the
# null dereference after it is not reported; the same function without the check is.
cat >analyzer.smith <<'C'
import smith::test;

typedef struct {
    i32 x;
} Box;

Box *find(i32 key);
void missing(void);

static i32 checked(bool flag) {
    Box *p = nullptr;
    if (flag) p = find(1);
    test::check(p != nullptr);
    return p->x;
}

static i32 unchecked(bool flag) {
    Box *p = nullptr;
    if (flag) p = find(1);
    if (p == nullptr) missing();
    return p->x;
}

i32 analyzed(void) {
    return checked(true) + unchecked(true);
}
C
capture "$smith" lint --checks='-*,clang-analyzer-*' analyzer.smith -- -std=gnu2y -I.smith/include
expect_contains "analyzer: a dereference after a plain test of null is reported" "$out" "analyzer.smith:21:12: warning: Access to field 'x' results in a dereference of a null pointer"
expect_lacks "analyzer: ... and after test::check(p != nullptr) it is not" "$out" "analyzer.smith:14:12"
fi

if part reports; then
# 10. Failure reports: a failure is a stack of addresses, and smith turns it into the failing line and the call chain.
# The plain-function checks print no file and line themselves, so the runner on its own prints the raw trace; smith
# symbolize (and smith test, which pipes the runner through it) reads the source.
project "$work/reports"
cat >tests/test_reports.smith <<'C'
import <assert.h>;
import smith::random;
import smith::test;

[[gnu::always_inline]] static inline void check_positive(i32 value) {
    test::check(value > 0);
}

void test_passes(void) {
    test::check(true);
    test::check_equal(4, 4);
    test::check_equal(0.5, 0.5);
    test::check_near(1.0f, 1.0001f, 0.001f);
    test::check_same_bits(-0.0, -0.0);
}

void test_check_fails(void) {
    i32 moves = 0;
    test::check(moves > 0);
}

void test_equal_fails(void) {
    u64 count = 3;
    u64 expected = 4;
    test::check_equal(count, expected);
}

void test_near_fails(void) {
    test::check_near(1.0, 1.5, 0.1);
}

void test_same_bits_fails(void) {
    test::check_same_bits(0.0f, -0.0f);
}

void test_assert_fails(void) {
    i32 moves = 1;
    c::assert(moves == 2);
}

void test_segfault(void) {
    volatile i32 *nothing = nullptr;
    *nothing = 7;
}

void test_inlined(void) {
    check_positive(-1);
}

void test_ordering_fails(void) {
    i32 total = 2 + 2;
    test::check_less_equal(total, 3);
}

void test_prop_function(random::Key key) {
    i64 x = random::uniform_int64_sample(key, (random::UniformInt64){.low = 0, .high = 1000});
    test::note("x = %lld", x);
    test::check(x < 500);
}
C
capture "$smith" build -DSAN=0
expect "reports: the runner builds" 0 "$rc"
capture build/tests $iso --test passes --verbose
expect_contains "reports: the plain-function checks pass" "$out" "PASS passes"

# Without smith in the pipe, a failure prints the trace and the values.
capture build/tests $iso --test check_fails
expect_contains "raw: a failed smith_check prints a trace block" "$out" "  smith-trace begin check failed"
expect_contains "raw: ... with frames" "$out" "  smith-trace frame 0x"
expect_contains "raw: ... and the image's UUID" "$out" "  smith-trace image "
capture build/tests $iso --test equal_fails
expect_contains "raw: a failed check_equal shows a" "$out" "    a = 3   (u64)"
expect_contains "raw: ... and b" "$out" "    b = 4   (u64)"
capture build/tests $iso --test ordering_fails
expect_contains "raw: a failed ordering check shows a" "$out" "    a = 4   (i32)"
expect_contains "raw: ... and b" "$out" "    b = 3   (i32)"
capture build/tests $iso --test prop_function --shrink-seconds 3600
expect_contains "raw: a property test's failed function check shrinks" "$out" "shrunk in"
expect_contains "raw: ... to the smallest case" "$out" "    note: x = 500"
capture build/tests $iso --test near_fails
expect_contains "raw: a failed check_near shows the distance" "$out" "= 0.5 [0x1p-1]"
capture build/tests $iso --test same_bits_fails
expect_contains "raw: a failed check_same_bits shows the bits" "$out" "    a = 0 [0x0p+0]"
expect "reports: --list sees the new tests" yes "$(build/tests --list | grep -q segfault && echo yes)"

if "$smith" symbolize --available; then
    sym() { # test name: the report smith test prints
        capture "$smith" test $iso -DSAN=0 --test "$1"
        printf '%s\n%s\n' "$out" "$err"
    }
    line=$(line_of tests/test_reports.smith 'test::check(moves > 0);')
    report=$(sym check_fails)
    expect_contains "report: a failed test::check names the line" "$report" "  check failed at tests/test_reports.smith:$line"
    expect_contains "report: ... shows the source line" "$report" "$line |     test::check(moves > 0);"
    expect_contains "report: ... and the call chain" "$report" "  called from"
    expect_contains "report: ... whose first frame is the test" "$report" "tests/test_reports.smith:$line"
    expect_contains "report: ... and whose last is the runner" "$report" "smith_test_execute"
    expect_lacks "report: ... without the check machinery of smith::test" "$report" "smith::test::"
    expect_lacks "report: ... or the raw trace" "$report" "smith-trace"

    line=$(line_of tests/test_reports.smith 'test::check_equal(count, expected);')
    report=$(sym equal_fails)
    expect_contains "report: a failed check_equal names the line" "$report" "  check failed at tests/test_reports.smith:$line"
    expect_contains "report: ... and prints both values under the source line, named by their text" "$report" "$line |     test::check_equal(count, expected);
            count = 3, expected = 4   (u64)
  called from"

    line=$(line_of tests/test_reports.smith 'c::assert(moves == 2);')
    report=$(sym assert_fails)
    expect_contains "report: an assert names the line" "$report" "  aborted at tests/test_reports.smith:$line"
    expect_contains "report: ... shows it" "$report" "$line |     c::assert(moves == 2);"
    expect_contains "report: ... and keeps assert's own message" "$report" "Assertion failed: (moves == 2)"
    expect_lacks "report: ... without libc's abort path" "$report" "pthread_kill"

    line=$(line_of tests/test_reports.smith '*nothing = 7;')
    report=$(sym segfault)
    expect_contains "report: a segfault names the line" "$report" "FAIL segfault: killed by SIGSEGV
  crashed at tests/test_reports.smith:$line"
    expect_contains "report: ... shows it" "$report" "$line |     *nothing = 7;"

    line=$(line_of tests/test_reports.smith 'test::check(value > 0);')
    caller=$(line_of tests/test_reports.smith 'check_positive(-1);')
    report=$(sym inlined)
    expect_contains "report: an inlined check names the inlined line" "$report" "  check failed at tests/test_reports.smith:$line"
    expect_contains "report: ... and the chain has the inlined function" "$report" "check_positive"
    expect_contains "report: ... and the function it was inlined into" "$report" "tests/test_reports.smith:$caller"

    line=$(line_of tests/test_reports.smith 'test::check_less_equal(total, 3);')
    report=$(sym ordering_fails)
    expect_contains "report: an ordering check's failure has the same report" "$report" "  check failed at tests/test_reports.smith:$line"
    expect_contains "report: ... and its values" "$report" "    total = 4, 3   (i32)"

    # Objective-C frames and methods.
    project "$work/objc"
    sed -e 's|tests/test_\*.c", "tests/test_\*.smith|tests/test_*.m|' -e 's|\.link_flags = .*|.link_flags = smith_list(b, "-framework", "Foundation", nullptr),|' build.c >build.c.new && mv build.c.new build.c
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
    line=$(line_of tests/test_reports.smith 'test::check(moves > 0);')
    capture env SMITH_LINKER=/usr/bin/ld "$smith" test $iso -DSAN=0 --test check_fails
    expect_contains "report: a program linked by Apple's ld has lines too" "$out$err" "  check failed at tests/test_reports.smith:$line"
    rm -f build/tests

    # A trace saved from a run is symbolized later, given the program.
    capture "$smith" build -DSAN=0
    capture sh -c "build/tests $iso --test check_fails --trace >'$work/trace.txt' 2>&1"
    saved=$("$smith" symbolize "$work/trace.txt")
    expect_contains "symbolize: a saved trace becomes a report" "$saved" "  check failed at tests/test_reports.smith:$line"
    expect_contains "symbolize: ... from standard input too" "$("$smith" symbolize <"$work/trace.txt")" "called from"
    cp build/tests "$work/moved_tests"
    mv build/tests build/tests_gone
    capture "$smith" symbolize "$work/trace.txt"
    expect_contains "symbolize: a program that is gone is said so" "$out" "note: cannot read"
    capture "$smith" symbolize --binary "$work/moved_tests" "$work/trace.txt"
    expect_contains "symbolize: --binary names it, and its UUID is checked" "$out" "  check failed at tests/test_reports.smith:$line"
    mv build/tests_gone build/tests

    # A source file newer than the program may have changed since: the report says so.
    sleep 1
    touch tests/test_reports.smith
    capture sh -c "build/tests $iso --test check_fails --trace 2>&1 | '$smith' symbolize"
    expect_contains "newer: a source newer than the program is noted" "$out" "note: tests/test_reports.smith is newer than the program, so line $line may have changed"

    # A trace of another program: the UUID differs, and no line is made up.
    printf '\n' >>tests/test_reports.smith
    printf 'void test_extra(void) {\n    test::check(true);\n}\n' >>tests/test_reports.smith
    capture "$smith" build -DSAN=0
    capture "$smith" symbolize "$work/trace.txt"
    expect_contains "symbolize: a program that is not the one that ran is noted" "$out" "is not the program that ran"
    expect_lacks "symbolize: ... and no line is claimed" "$out" "check failed at tests/"

    # smith as llvm-symbolizer: the sanitizers' protocol.
    address=$(nm build/tests | awk '$3 == "_test_check_fails" {print $1}')
    ln -s "$smith" "$work/llvm-symbolizer"
    capture sh -c "printf 'CODE \"%s:arm64\" 0x%s\n' '$PWD/build/tests' '$address' | '$work/llvm-symbolizer' --demangle --inlines"
    expect_contains "protocol: a CODE request names the function" "$out" "test_check_fails"
    expect_contains "protocol: ... and the file, line and column" "$out" "tests/test_reports.smith:"
    capture sh -c "printf '\"%s\" 0x1\n' '/nonexistent' | '$work/llvm-symbolizer'"
    expect "protocol: an unknown module gives ?? and an empty line" "??
??:0:0
" "$out
"
fi

# A failed contract is a failure like a failed check: smith_failure gives it the same report, and a test that called
# smith_test_expect_abort still passes on it, as it does on an assert.
if "$smith" symbolize --available; then
    project "$work/contracts"
    {
        sed -e '/^    SmithTests tests = {/,$d' build.c
        cat <<'C'
    SmithObject contract = {.compiler = "smith-cc", .source = smith_library(b, "contract"), .output = "build/smith/contract.o", .flags = flags};
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
    smith_test_expect_abort();
    (void)halve(5);
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
    expect_contains "contract: smith_test_expect_abort passes on a failed contract, and the true one passes" "$out$err" "2 passed, 1 failed"
    expect_lacks "contract: ... with no smith_failure frame in the chain" "$out$err" "smith_failure"
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
fi

if part flags; then
# 11. The libraries compile clean under the flags of the conventions (smith_profile_warnings), in both C standards, and under the extra
# warnings game and learner add, since every project compiles them with its own flags.
flags=$(awk '/smith_profile_warnings\[\] =/ {on = 1} on {print} /nullptr};/ {on = 0}' "$repo/runtime/build_runtime.c" | grep -o '"-[^"]*"' | tr -d '"' | grep -v '^-std=')
flags="$flags -Wdouble-promotion -Wundef -Wpointer-arith -Wcast-qual -Wformat=2 -Wmissing-variable-declarations -Wcomma -Wunreachable-code"
for standard in gnu2y gnu23; do
    for library in test random arena trace; do
        source=$repo/lib/$library.c
        case $library in random | arena | test) source=$repo/lib/smith/$library.smith ;; esac # .smith modules
        # shellcheck disable=SC2086
        capture "$smith" cc -std=$standard $flags -I "$repo/include" -c "$source" -o "$work/$library.o"
        expect "flags: ${source#"$repo"/} compiles clean under $standard with the conventions' warnings" 0 "$rc"
        [ "$rc" -eq 0 ] || printf '%s\n%s\n' "$out" "$err"
    done
done

fi

if part inprocess; then
# 12. --in-process: every test in the runner's own process, no fork, so a debugger that does not follow forks (lldb on
# macOS) stops inside a test. smith dbg is in the full smith only.
project "$work/inprocess"
cat >tests/test_inproc.smith <<'C'
import <stdio.h>;
import <stdlib.h>;
import <unistd.h>;
import smith::test;

void test_inproc_pass(void) {
    c::printf("in-process output\n");
    test::check_equal(1 + 1, 2);
}

void test_inproc_fail(void) {
    i32 total = 40;
    total += 2;
    test::check_equal(total, 41);
}

void test_inproc_local(void) {
    i32 answer = 41;
    answer += 1;
    test::check_equal(answer, 42);
}

void test_inproc_pid(void) {
    c::printf("pid %d\n", (i32)c::getpid());
}

void test_inabort_abort(void) {
    test::expect_abort();
    c::abort();
}

void test_inabort_trap(void) {
    test::expect_abort();
    __builtin_trap();
}

void test_inabort_returns(void) {
    test::expect_abort();
}

void test_inabort_check_after(void) {
    test::expect_abort();
    test::check_equal(1, 2);
}
C
capture "$smith" build
expect "in-process: the runner builds" 0 "$rc"
capture build/tests --in-process --test inproc_
expect_contains "in-process: a failed check is reported" "$out" "FAIL inproc_fail"
expect_contains "in-process: ... with its values" "$out" "41"
expect_contains "in-process: the other tests ran around it" "$out" "3 passed, 1 failed"
expect_contains "in-process: output goes to the terminal as written" "$out" "in-process output"
capture build/tests --in-process --test inabort_ --verbose --stats
expect_contains "in-process: an expected abort passes" "$out" "PASS inabort_abort"
expect_contains "in-process: ... a trap too" "$out" "PASS inabort_trap"
expect_contains "in-process: ... returning fails" "$out" "FAIL inabort_returns"
expect_contains "in-process: ... saying so" "$out" "expected an abort; the test returned"
expect_contains "in-process: ... a failed check after the call fails" "$out" "expected an abort; a check failed, which is not an abort"
expect_contains "in-process: ... and nothing forked" "$out" "forks: 0"
capture build/tests --in-process --test inproc_pass --stats
expect_contains "in-process: no fork" "$out" "forks: 0"
capture build/tests --test inproc_pass --stats
expect_contains "in-process: without the flag the runner forks a worker" "$out" "forks: 1"
if "$smith" dbg --version >/dev/null 2>&1; then
    line=$(line_of tests/test_inproc.smith 'test::check_equal(answer, 42);')
    capture "$smith" dbg -b -o "b test_inproc.smith:$line" -o run -o 'frame variable answer' -o kill -- build/tests --test inproc_local --in-process
    expect_contains "in-process: smith dbg stops at a breakpoint inside a test" "$out" "stop reason = breakpoint 1.1"
    expect_contains "in-process: ... and shows its local" "$out" "(i32) answer = 42"
fi
fi

if part explain; then
# 12b. A failed check or contract shows the variables of its line, read from the stopped test by lldb (--explain <smith>:
# `smith test` passes it): `check(a == b)` prints a and b, a member chain, an array element, an enum and a pointer print
# too, a crash reports as before, and a machine that refuses to debug (or a smith with no debugger) gets the report and
# one note saying why.
if ! "$smith" --smith-explain --available; then
    echo "skip  explain: this smith has no debugger"
else
project "$work/explain"
cat >tests/test_explain.smith <<'C'
import smith::contract;
import smith::test;

typedef struct {
    u64 count;
    u64 capacity;
} Moves;

typedef enum : u8 { Red, Green } Color;

typedef struct {
    i32 cells[3][3];
    Moves *moves;
    Color color;
} Board;

static i32 clamp(usize count) {
    contract::requires(count <= 5);
    return (i32)count;
}

void test_ex_member(void) {
    Moves moves = {.count = 7, .capacity = 9};
    u64 expected = 8;
    test::check(moves.count == expected);
}

void test_ex_board(void) {
    Moves moves = {.count = 7, .capacity = 9};
    Board board = {.cells = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}}, .moves = &moves, .color = Green};
    Board *b = &board;
    usize row = 1;
    usize col = 2;
    test::check(b->cells[row][col] == 7 && b->color == Red && b->moves != nullptr);
}

void test_ex_contract(void) {
    usize count = 6;
    (void)clamp(count);
}

void test_ex_equal(void) {
    u64 count = 3;
    test::check_equal(count, 4);
}

void test_ex_crash(void) {
    i32 *p = nullptr;
    test::check_equal(*p, 1);
}
C
cat >tests/test_explain_c.c <<'C'
#include <smith/test.h>
#include <stdint.h>

typedef struct {
    uint64_t count;
    uint64_t capacity;
} Moves;

void test_exc_member(void) {
    Moves moves = {7, 9};
    uint64_t expected = 8;
    smith_check(moves.count == expected);
}
C
capture "$smith" build
expect "explain: the runner builds" 0 "$rc"
explained() { # runner arguments: the report smith test prints, with the values read from the stopped test
    build/tests $iso --trace --explain "$smith" "$@" 2>&1 | "$smith" symbolize 2>&1 | plain | tr -s ' '
}
out=$(explained --test ex_member)
case $out in
*"values not shown"*)
    expect_contains "explain: a machine that may not debug gets a note and the report" "$out" "check failed at tests/test_explain.smith:"
    echo "skip  explain: this account may not debug ($(printf '%s\n' "$out" | grep 'values not shown'))"
    ;;
*)
    expect_contains "explain: check(a == b) prints the member chain" "$out" "moves.count = 7"
    expect_contains "explain: ... and the other variable" "$out" "expected = 8"
    expect_lacks "explain: ... and not the sentence the values replace" "$out" "the condition is false"
    out=$(explained --test ex_board)
    expect_contains "explain: an array element, an index read from a variable" "$out" "b->cells[row][col] = 6"
    expect_contains "explain: an enum prints its name" "$out" "b->color = "
    expect_contains "explain: ... Green" "$out" "Green"
    expect_contains "explain: a pointer prints its address and what it points to" "$out" "b->moves = 0x"
    expect_contains "explain: ... and the pointee" "$out" "-> (count = 7, capacity = 9)"
    out=$(explained --test ex_contract)
    expect_contains "explain: a failed contract prints the variable of its line" "$out" "count = 6"
    expect_lacks "explain: ... without repeating the heading" "$out" "requires failed
"
    out=$(explained --test exc_member)
    expect_contains "explain: the same in C" "$out" "moves.count = 7"
    expect_contains "explain: ... both variables" "$out" "expected = 8"
    out=$(explained --test ex_equal)
    expect_contains "explain: check_equal keeps its own two values" "$out" "count = 3, 4 (u64)"
    expect_lacks "explain: ... and reads nothing" "$out" "@"
    plain_crash=$(build/tests $iso --test ex_crash 2>&1 | plain | grep -v 'smith-trace')
    read_crash=$(build/tests $iso --explain "$smith" --test ex_crash 2>&1 | plain | grep -v 'smith-trace')
    expect "explain: a crash reports as it did" "$plain_crash" "$read_crash"
    out=$(explained --test ex_ --jobs 1 --explain-max 1)
    expect_contains "explain: a run reads at most --explain-max failures" "$out" "more failures were not read in the debugger (--explain-max 1)"
    out=$(build/tests $iso --trace --test ex_member --explain "$smith" --no-explain 2>&1 | "$smith" symbolize 2>&1)
    expect_contains "explain: --no-explain gives the report as before" "$out" "the condition is false"
    expect_lacks "explain: ... without the values" "$out" "moves.count = 7"
    ;;
esac
capture "$smith" test $iso --test ex_member
expect_contains "explain: smith test reads the values without being asked" "$out" "expected    = 8"
fi
fi

# 13. Tests in .smith files: found by name in a file with no module line and in a module (`export void test_x(void) {`,
# its C name a_b_test_x and its symbol a::b::test_x), shown and selected by the name as written, and a failure's report
# names the .smith line and the function as written. A library in the module `smith::test` is machinery, like the C one.
if part lang; then
if ! "$smith" cc --smith-which -x c 2>/dev/null | grep -q "token-hook yes"; then
    echo "skipped: this smith's clang has no token hook, so no .smith tests"
else
    mkdir -p "$work/lang/tests/geometry" "$work/lang/src/geometry" "$work/lang/src/smith/test" && cd "$work/lang" || exit 1
    cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu2y", "-g", "-Wall", "-Wextra", "-Werror", "-I", ".smith/include", nullptr);
    SmithObject box = {.compiler = "smith-cc", .source = "src/geometry/box.smith", .output = "build/box.o", .flags = flags};
    smith_object(b, &box);
    SmithObject checks = {.compiler = "smith-cc", .source = "src/smith/test/helpers.smith", .output = "build/checks.o", .flags = flags};
    smith_object(b, &checks);
    SmithTests tests = {
        .sources = smith_glob(b, smith_list(b, "tests/test_*.c", "tests/test_*.smith", "tests/**/*_test.smith", nullptr)),
        .objects = (const SmithObject *[]){&box, &checks, nullptr},
        .flags = flags,
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
C
    cat >src/geometry/box.smith <<'C'
module geometry::box;

export u64 area(u64 width, u64 height) {
    return width * height;
}
C
    cat >src/smith/test/helpers.smith <<'C'
module smith::test::helpers;
import smith::test;

// A check of a library written as a module under smith::test: its frames are machinery, never the failing line.
export void require(bool condition) {
    test::check(condition);
}
C
    cat >tests/test_plain.smith <<'C'
import smith::random;
import smith::test;

void test_plain_works(void) {
    test::check_equal(2 + 2, 4);
}

void test_plain_fails(void) {
    u64 n = 3;
    test::check_equal(n, 4);
}

void test_plain_property(random::Key key) {
    test::check_equal(random::key_bits(key, 0), random::key_bits(key, 0));
}
C
    cat >tests/test_c_header.smith <<'C'
import <smith/test.h>;

// The C header of the library imported as a C header: the names under c:: and the C spelling of the key are found too.
void test_c_header_key(c::SmithKey key) {
    c::smith_check_equal_u64(c::smith_key_bits(key, 0), c::smith_key_bits(key, 0));
}
C
    cat >tests/geometry/box_test.smith <<'C'
module geometry::box_test;
import geometry::box;
import smith::random;
import smith::test;
import smith::test::helpers as helpers;

export void test_area(void) {
    u64 area = box::area(2, 3);
    test::check_equal_u64(area, 6); // the named instance: the header pass of a module cannot lower a generic call
}

export void test_area_fails(void) {
    u64 expected = 7;
    u64 area = box::area(2, 3);
    test::check_equal_u64(area, expected);
}

export void test_area_through_a_library(void) {
    helpers::require(box::area(2, 3) == 7);
}

export void test_area_property(random::Key key) {
    u64 side = random::key_bits(key, 0) % 1000;
    u64 area = box::area(side, 1);
    test::check_equal_u64(area, side);
}

// Not exported: private to the module, nothing can call it, so it is not a test.
[[maybe_unused]] void test_private(void) {
}
C
    capture "$smith" build
    expect "smith tests: the runner builds" 0 "$rc"
    if [ "$rc" -ne 0 ]; then printf '%s\n%s\n' "$out" "$err"; fi
    capture build/tests $iso --list
    expect "smith tests: found, a module's by its written name" "geometry::box_test::area
geometry::box_test::area_fails
geometry::box_test::area_through_a_library
geometry::box_test::area_property
c_header_key
plain_works
plain_fails
plain_property" "$out"
    capture grep -F 'void geometry_box_test_test_area(void)' build/tests_list.h
    expect_contains "smith tests: ... the prototype has the symbol of the export" "$out" '__asm__("_geometry::box_test::test_area")'
    capture build/tests $iso --test geometry::box_test
    expect_contains "smith tests: --test selects by the written name (the module: four tests)" "$out" "2 passed, 2 failed"
    capture build/tests $iso --test geometry::box_test::test_area_fails
    expect_contains "smith tests: --test takes the name with test_ as written in the source" "$out" "FAIL geometry::box_test::area_fails"
    expect_contains "smith tests: ... and runs that one only" "$out" "0 passed, 1 failed"
    capture build/tests $iso --test test_plain_works
    expect_contains "smith tests: a test of a file with no module by the name in the source" "$out" "1 passed, 0 failed"
    capture build/tests $iso --test plain_property --seeds 5 --verbose
    expect_contains "smith tests: a property test in a .smith file runs on keys" "$out" "PASS plain_property (5 seeds)"
    capture build/tests $iso --test geometry::box_test::area_property --seeds 5 --verbose
    expect_contains "smith tests: ... and so does a module's" "$out" "PASS geometry::box_test::area_property (5 seeds)"
    capture build/tests $iso --list
    expect_lacks "smith tests: a private test is not found" "$out" "private"

    cp tests/test_plain.smith tests/test_again.smith
    capture "$smith" build
    expect "smith tests: the same test in two files fails the build" 1 "$rc"
    expect_contains "smith tests: ... naming both files" "$out$err" "smith: smith_tests: test_plain_works is defined in tests/test_again.smith and in tests/test_plain.smith"
    rm tests/test_again.smith
    capture "$smith" build
    expect "smith tests: ... and builds again without it" 0 "$rc"

    if "$smith" symbolize --available; then
        capture "$smith" test --test geometry::box_test::area_fails
        line=$(line_of tests/geometry/box_test.smith 'test::check_equal_u64(area, expected);')
        expect_contains "smith tests: a failing check names the .smith line" "$out" "check failed at tests/geometry/box_test.smith:$line"
        expect_contains "smith tests: ... its source" "$out" "test::check_equal_u64(area, expected);"
        expect_contains "smith tests: ... the function as written, in the chain (the columns follow the longest path)" "$(printf '%s' "$out" | tr -s ' ')" "tests/geometry/box_test.smith:$line geometry::box_test::test_area_fails"
        capture "$smith" test --test geometry::box_test::area_through_a_library
        line=$(line_of tests/geometry/box_test.smith 'helpers::require(box::area(2, 3) == 7);')
        expect_contains "smith tests: a frame of the module smith::test is not the failing line" "$out" "check failed at tests/geometry/box_test.smith:$line"
        expect_lacks "smith tests: ... nor in the chain" "$out" "smith::test::"
        capture "$smith" test --test plain_fails
        line=$(line_of tests/test_plain.smith 'test::check_equal(n, 4);')
        expect_contains "smith tests: a test of a file with no module is named as written" "$(printf '%s' "$out" | tr -s ' ')" "tests/test_plain.smith:$line test_plain_fails"
        capture "$smith" test --test geometry::box_test::test_area_property
        expect_contains "smith tests: smith test passes the written name through" "$out" "1 passed, 0 failed"
    fi

    # 14. A test that takes an io: `void test_x(c::SmithIo *io)` (or `io::Io *io`) gets a real io made for it alone, on the run's seed, with a
    # scratch folder of its own (also $TMPDIR), removed when the test passes and kept, with its path printed, when it fails.
    # The tests write what they saw into $SMITH_TEST_PROBE/<name>, which the checks below read.
    project "$work/iotests"
    mkdir "$work/probe" && probe_folder=$work/probe
    echo hello >tests/fixture.txt
    cat >tests/test_io.smith <<'C'
import <stdio.h>;
import <stdlib.h>;
import <string.h>;
import <sys/stat.h>;
import <unistd.h>;
import smith::arena;
import smith::io;
import smith::test;

// Writes text to $SMITH_TEST_PROBE/name, where the script that runs the tests reads it.
static void probe(const char *name, const char *text) {
    const char *folder = c::getenv("SMITH_TEST_PROBE");
    test::check_not_equal(folder, nullptr);
    char path[1024];
    c::snprintf(path, sizeof path, "%s/%s", folder, name);
    c::FILE *file = c::fopen(path, "w");
    test::check_not_equal(file, nullptr);
    c::fputs(text, file);
    c::fclose(file);
}

static bool exists(const char *path) {
    return c::access(path, c::F_OK) == 0;
}

void test_io_real(c::SmithIo *io) {
    const char *scratch = test::scratch(io);
    test::check_not_equal(scratch, nullptr);
    struct c::stat info;
    test::check_equal(c::stat(scratch, &info), 0);
    test::check(c::S_ISDIR(info.st_mode));
    const char *tmp = c::getenv("TMPDIR");
    test::check_not_equal(tmp, nullptr);
    test::check_equal(c::strcmp(tmp, scratch), 0);
    u64 *cell = arena::push(&io->arena, u64); // the io's arena is real memory
    test::check_not_equal(cell, nullptr);
    char marker[1024];
    c::snprintf(marker, sizeof marker, "%s/marker.txt", scratch);
    probe("real", scratch);
    c::FILE *file = c::fopen(marker, "w");
    test::check_not_equal(file, nullptr);
    c::fclose(file);
    test::check(exists(marker));
}

void test_io_fails(c::SmithIo *io) {
    const char *scratch = test::scratch(io);
    probe("fails", scratch);
    char marker[1024];
    c::snprintf(marker, sizeof marker, "%s/marker.txt", scratch);
    c::FILE *file = c::fopen(marker, "w");
    test::check_not_equal(file, nullptr);
    c::fclose(file);
    test::check_equal(1, 2);
}

void test_io_seed(c::SmithIo *io) {
    char text[128];
    c::snprintf(text, sizeof text, "%llu %llu", io->seed, io->key.bits);
    probe("seed", text);
    test::check_equal(io->key.bits, io->seed);
}

void test_io_fixture(c::SmithIo *io) {
    c::FILE *file = c::fopen("tests/fixture.txt", "r");
    test::check_not_equal(file, nullptr);
    char text[16] = "";
    test::check_not_equal(c::fgets(text, sizeof text, file), nullptr);
    c::fclose(file);
    test::check_equal(c::strcmp(text, "hello\n"), 0);
    char directory[1024];
    test::check_not_equal(c::getcwd(directory, sizeof directory), nullptr);
    probe("cwd", directory);
    test::check_not_equal(test::scratch(io), nullptr);
}

// Two io tests that wait for each other: each can finish only while the other is running, so they run side by side.
static void wait_for(const char *name) {
    const char *folder = c::getenv("SMITH_TEST_PROBE");
    char path[1024];
    c::snprintf(path, sizeof path, "%s/%s", folder, name);
    for (i32 i = 0; i < 300 && !exists(path); i++) {
        c::usleep(10000);
    }
    test::check(exists(path));
}

void test_io_parallel_a(c::SmithIo *io) {
    probe("a", test::scratch(io));
    wait_for("b");
}

void test_io_parallel_b(c::SmithIo *io) {
    probe("b", test::scratch(io));
    wait_for("a");
}

// The same test written with the module's own name for the type, which import smith::io; gives.
void test_io_module_name(io::Io *io) {
    probe("module_name", test::scratch(io));
    test::check_not_equal(test::scratch(io), nullptr);
}
C
    capture "$smith" build
    expect "io tests: the runner builds" 0 "$rc"
    if [ "$rc" -ne 0 ]; then printf '%s\n%s\n' "$out" "$err"; fi
    capture build/tests $iso --list
    expect_contains "io tests: an io test is found beside the others" "$out" "io_real"
    expect_contains "io tests: ... one written io::Io *io (import smith::io;) too" "$out" "io_module_name"
    here=$(pwd -P)
    io_run() { # runner arguments
        capture env SMITH_TEST_PROBE="$probe_folder" build/tests $iso "$@"
    }

    io_run --test io_real --verbose
    expect_contains "io tests: a test gets a real io, with a scratch folder that is its TMPDIR" "$out" "PASS io_real"
    scratch=$(cat "$probe_folder/real" 2>/dev/null)
    expect "io tests: ... the folder is removed after a pass" "gone" "$([ -n "$scratch" ] && [ ! -e "$scratch" ] && echo gone)"

    io_run --test io_fails
    scratch=$(cat "$probe_folder/fails" 2>/dev/null)
    expect_contains "io tests: a failure prints the folder it kept" "$out" "scratch folder (kept): $scratch"
    expect "io tests: ... which exists, with what the test wrote" "yes" "$([ -n "$scratch" ] && [ -f "$scratch/marker.txt" ] && echo yes)"
    expect_contains "io tests: ... and the failure names the seed to replay" "$out" "rerun: smith test --test io_fails --seed "
    [ -n "$scratch" ] && rm -rf "$scratch"

    io_run --test io_seed --seed 12345 --verbose
    expect_contains "io tests: --seed N runs an io test on N" "$out" "PASS io_seed (seed 12345)"
    expect "io tests: ... its seed is N and so are the bits of its key" "12345 12345" "$(cat "$probe_folder/seed" 2>/dev/null)"
    rm -f "$probe_folder/seed"
    io_run --test io_seed --seed 12345
    expect "io tests: ... and again, the same" "12345 12345" "$(cat "$probe_folder/seed" 2>/dev/null)"
    io_run --test io_seed --seed 6
    expect "io tests: ... another seed, another key" "6 6" "$(cat "$probe_folder/seed" 2>/dev/null)"
    io_run --test io_seed
    seeds=$(cat "$probe_folder/seed" 2>/dev/null)
    expect "io tests: ... without --seed the key still comes from the seed" "yes" "$([ "${seeds% *}" = "${seeds#* }" ] && [ -n "$seeds" ] && echo yes)"

    io_run --test io_module_name
    expect_contains "io tests: a test that takes io::Io * gets a real io" "$out" "1 passed, 0 failed"
    expect "io tests: ... with a scratch folder of its own" "yes" "$([ -n "$(cat "$probe_folder/module_name" 2>/dev/null)" ] && echo yes)"

    io_run --test io_fixture
    expect_contains "io tests: the working directory is the project root, so a fixture resolves" "$out" "1 passed, 0 failed"
    expect "io tests: ... the root itself" "$here" "$(cat "$probe_folder/cwd" 2>/dev/null)"

    rm -f "$probe_folder/a" "$probe_folder/b"
    io_run --test io_parallel --jobs 2
    expect_contains "io tests: two io tests run side by side" "$out" "2 passed, 0 failed"
    expect "io tests: ... each in a folder of its own" "different" "$([ -n "$(cat "$probe_folder/a")" ] && [ "$(cat "$probe_folder/a")" != "$(cat "$probe_folder/b")" ] && echo different)"
    if [ -n "$have_symbolizer" ]; then
        capture env SMITH_TEST_PROBE="$probe_folder" "$smith" test $iso --test io_fixture
        expect "io tests: smith test runs an io test too" 0 "$rc"
    fi
fi
fi

echo
echo "$total test checks, $failures failed"
[ "$failures" -eq 0 ]
