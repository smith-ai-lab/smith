#!/bin/sh
# Tests of <smith/text.h>, with real clang and ninja. Usage: sh tests/text.sh path/to/smith [exhaustive]
# The checks are tests/text.c, built as a scratch project's test file with smith_tests (smith's own test library), in
# three builds: -O0 under AddressSanitizer and UBSan, -O2, and -O2 -DNDEBUG. With "exhaustive" it instead parses every
# finite float, 2^32 patterns on all cores, against strtof: a few minutes, so it is not part of `smith test`.
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
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
        printf '%s\n%s\n' "$out" "$err"
    fi
}

mkdir "$work/bin" && ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" && ln -s "$smith" "$work/bin/smith-c++"
PATH="$work/bin:$PATH"
cc=${CC:-cc}

if [ "${2:-}" = exhaustive ]; then
    $cc -std=gnu23 -O2 -Wall -Wextra -Werror -I "$here/include" "$here/tests/text_exhaustive.c" "$here/lib/text.c" \
        -pthread -o "$work/exhaustive" || exit 1
    start=$(date +%s)
    "$work/exhaustive"
    status=$?
    echo "exhaustive: exit $status in $(($(date +%s) - start)) s"
    exit "$status"
fi

# The scratch project: smith's text library, and the runner built from tests/test_text.c.
mkdir -p "$work/project/tests" && cd "$work/project" || exit 1
cp "$here/tests/text.c" tests/test_text.c
cat >build.c <<'C'
#include <smith/build.h>

#include <string.h>

void build(SmithBuild *b) {
    const char *mode = smith_option_value(b, "MODE", "asan", "asan, fast or ndebug");
    bool sanitize = strcmp(mode, "asan") == 0;
    const char *const *flags = smith_cat(
        b, smith_list(b, "-std=gnu23", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Wshadow", "-Wconversion", "-Werror", "-I", ".smith/include", nullptr),
        sanitize ? smith_list(b, "-O0", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", nullptr)
                 : smith_list(b, "-O2", nullptr),
        strcmp(mode, "ndebug") == 0 ? smith_list(b, "-DNDEBUG", nullptr) : smith_list(b, nullptr), nullptr);
    const char *const *names = smith_list(b, "arena", "random", "text", nullptr);
    SmithObject objects[3] = {};
    for (size_t i = 0; names[i] != nullptr; i++) {
        objects[i] = (SmithObject){.compiler = "smith-cc", .source = smith_library(b, names[i]),
                                   .output = smith_fmt(b, "build/smith/%s.o", names[i]), .flags = flags};
        smith_object(b, &objects[i]);
    }
    SmithTests tests = {
        .sources = smith_list(b, "tests/test_text.c", nullptr),
        .objects = (const SmithObject *[]){&objects[0], &objects[1], &objects[2], nullptr},
        .flags = flags,
        .link_flags = smith_when(b, sanitize, smith_list(b, "-fsanitize=address,undefined", nullptr)),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
C

count=$(grep -c '^void test_' "$here/tests/text.c")
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

# The full size of the sweep, in the fast build.
capture smith build -DMODE=fast
capture ./build/tests --full --test f32_sweep
summary=$(printf '%s\n' "$out" | grep ' passed, ' | sed 's/ in [0-9.]* s$//')
expect "full: the sweep of the float patterns, four times denser" "1 passed, 0 failed" "$summary"

# The library alone: it compiles in C23 and C2y, and with the checks off, using no library function of libc.
for std in gnu23 gnu2y; do
    capture $cc -std=$std -Wall -Wextra -Wpedantic -Wconversion -Werror -O2 -I "$here/include" -c "$here/lib/text.c" -o "$work/text.o"
    expect "library: compiles as $std" 0 "$rc"
done
capture $cc -std=gnu23 -O2 -DNDEBUG -ffreestanding -fno-stack-protector -I "$here/include" -c "$here/lib/text.c" -o "$work/text.o"
undefined=$(nm -u "$work/text.o" | tr -d ' ' | grep -v -x -e _memcpy -e _memset -e _bzero -e _memmove | sort | tr '\n' ' ') # but the compiler's own calls for a big struct
expect "library: names no libc function" "" "$undefined"

echo "$total tests, $failures failed"
[ "$failures" -eq 0 ]
