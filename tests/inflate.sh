#!/bin/sh
# Tests of <smith/inflate.h> (lib/smith/inflate.smith), with real clang. Usage: sh tests/inflate.sh path/to/smith
# The checks are tests/inflate_test.smith, built as a scratch project's test file with smith_tests (smith's own test
# library), with the fixtures of tests/fixtures/inflate, in three builds: -O0 under AddressSanitizer and UBSan, -O2, and
# -O2 -DNDEBUG. The library alone is also compiled as C by the header it generates.
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
export XDG_CACHE_HOME=$work/xdg-cache # the module cache of the tests, never the real one
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
        printf '%s\n%s\n' "$out" "$err"
    fi
}

mkdir "$work/bin" && ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" && ln -s "$smith" "$work/bin/smith-c++"
PATH="$work/bin:$PATH"

mkdir -p "$work/project/tests" && cd "$work/project" || exit 1
cp "$here/tests/inflate_test.smith" tests/inflate_test.smith
cp -R "$here/tests/fixtures" tests/fixtures
cat >build.c <<'C'
#include <smith/build.h>

#include <string.h>

void build(SmithBuild *b) {
    const char *mode = smith_option_value(b, "MODE", "asan", "asan, fast or ndebug");
    bool sanitize = strcmp(mode, "asan") == 0;
    const char *const *flags = smith_cat(
        b, smith_list(b, "-std=gnu2y", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Wshadow", "-Wconversion", "-Werror", "-I", ".smith/include", nullptr),
        sanitize ? smith_list(b, "-O0", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", nullptr)
                 : smith_list(b, "-O2", nullptr),
        strcmp(mode, "ndebug") == 0 ? smith_list(b, "-DNDEBUG", nullptr) : smith_list(b, nullptr), nullptr);
    const char *const *names = smith_list(b, "arena", "random", "inflate", nullptr);
    SmithObject objects[3] = {};
    for (size_t i = 0; names[i] != nullptr; i++) {
        objects[i] = (SmithObject){.compiler = "smith-cc", .source = smith_library(b, names[i]),
                                   .output = smith_fmt(b, "build/smith/%s.o", names[i]), .flags = flags};
        smith_object(b, &objects[i]);
    }
    SmithTests tests = {
        .sources = smith_list(b, "tests/inflate_test.smith", nullptr),
        .objects = (const SmithObject *[]){&objects[0], &objects[1], &objects[2], nullptr},
        .flags = flags,
        .link_flags = smith_when(b, sanitize, smith_list(b, "-fsanitize=address,undefined", nullptr)),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
C

count=$(grep -c '^export void test_' "$here/tests/inflate_test.smith")
for mode in asan fast ndebug; do
    capture smith build -DMODE=$mode
    expect "$mode: the tests build" 0 "$rc"
    capture ./build/tests
    summary=$(printf '%s\n' "$out" | grep ' passed, ' | sed 's/ in [0-9.]* s$//')
    expect "$mode: all $count tests pass" "$count passed, 0 failed" "$summary"
    printf '%s\n' "$out" | grep ' passed, ' | sed 's/^/      /'
    capture ./build/tests --isolate
    summary=$(printf '%s\n' "$out" | grep ' passed, ' | sed 's/ in [0-9.]* s$//')
    expect "$mode: all pass with a process per test" "$count passed, 0 failed" "$summary"
done

# More keys for the property tests, and more trials for the damage, under the sanitizers.
capture smith build -DMODE=asan
for prop in test_prop_inflate_round_trips_stored_blocks test_prop_inflate_checksums_equal_their_references_and_continue; do
    capture ./build/tests --test $prop --seeds 1000
    summary=$(printf '%s\n' "$out" | grep ' passed, ' | sed 's/ in [0-9.]* s$//')
    expect "$prop on 1000 keys under ASan and UBSan" "1 passed, 0 failed" "$summary"
done
for seed in 1 2 3 4 5; do
    capture ./build/tests --seed $seed --test test_inflate_survives_corrupt_input
    summary=$(printf '%s\n' "$out" | grep ' passed, ' | sed 's/ in [0-9.]* s$//')
    expect "damaged input never misbehaves (--seed $seed)" "1 passed, 0 failed" "$summary"
done

echo "$total tests, $failures failed"
[ "$failures" -eq 0 ]
