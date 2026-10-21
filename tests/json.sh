#!/bin/sh
# Tests of <smith/json.h>, with real clang and ninja. Usage: sh tests/json.sh path/to/smith
# The checks are tests/json.c, built as a scratch project's test file with smith_tests (smith's own test library), in
# three builds: -O0 under AddressSanitizer and UBSan, -O2, and -O2 -DNDEBUG. JSONTestSuite is fetched as a package
# pinned by hash (into $SMITH_JSON_SUITE_CACHE, else ~/.cache/smith-tests, so that it is downloaded once; with no
# network the conformance test says so and is skipped). The asan build also runs the fuzz tests on many more keys.
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

# JSONTestSuite, pinned by the hash of its archive.
suite_url=https://github.com/nst/JSONTestSuite/archive/1ef36fa01286573e846ac449e8683f8833c5b26a.tar.gz
suite_hash=sha256:32ab2fb37cc26679dbd9164dbb7053bcef10134643d53cb0cef45de7680dcdcc
suite_cache=${SMITH_JSON_SUITE_CACHE:-$HOME/.cache/smith-tests}
suite=
mkdir -p "$suite_cache"
fetched=$(XDG_CACHE_HOME=$suite_cache smith fetch "$suite_url" 2>&1)
case $fetched in
*"$suite_hash"*) suite=$(printf '%s\n' "$fetched" | sed -n 's/^smith: .* is in //p')/test_parsing ;;
*) echo "SKIP  JSONTestSuite is not available (no network?): $fetched" ;;
esac
if [ -n "$suite" ] && [ ! -d "$suite" ]; then suite=; fi
if [ -n "$suite" ]; then
    echo "      JSONTestSuite: $suite"
else
    echo "      conformance is skipped"
fi

# The scratch project: smith's json library and what it needs, and the runner built from tests/json.c.
mkdir -p "$work/project/tests" && cd "$work/project" || exit 1
cp "$here/tests/json.c" tests/test_json.c
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
    const char *const *names = smith_list(b, "arena", "random", "text", "json", nullptr);
    SmithObject objects[4] = {};
    for (size_t i = 0; names[i] != nullptr; i++) {
        objects[i] = (SmithObject){.compiler = "smith-cc", .source = smith_library(b, names[i]),
                                   .output = smith_fmt(b, "build/smith/%s.o", names[i]), .flags = flags};
        smith_object(b, &objects[i]);
    }
    SmithTests tests = {
        .sources = smith_list(b, "tests/test_json.c", nullptr),
        .objects = (const SmithObject *[]){&objects[0], &objects[1], &objects[2], &objects[3], nullptr},
        .flags = flags,
        .link_flags = smith_when(b, sanitize, smith_list(b, "-fsanitize=address,undefined", nullptr)),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
C

count=$(grep -c '^void test_' "$here/tests/json.c")
for mode in asan fast ndebug; do
    capture smith build -DMODE=$mode
    expect "$mode: the tests build" 0 "$rc"
    capture env SMITH_JSON_SUITE="$suite" ./build/tests
    summary=$(printf '%s\n' "$out" | grep ' passed, ' | sed 's/ in [0-9.]* s$//')
    expect "$mode: all $count tests pass" "$count passed, 0 failed" "$summary"
    printf '%s\n' "$out" | grep ' passed, ' | sed 's/^/      /'
    capture env SMITH_JSON_SUITE="$suite" ./build/tests --isolate
    summary=$(printf '%s\n' "$out" | grep ' passed, ' | sed 's/ in [0-9.]* s$//')
    expect "$mode: all pass with a process per test" "$count passed, 0 failed" "$summary"
done

# What was done with the suite: the counts, and each implementation-defined case.
if [ -n "$suite" ]; then
    capture env SMITH_JSON_SUITE="$suite" SMITH_JSON_LISTING="$work/listing" ./build/tests --test json_conformance
    summary=$(tail -1 "$work/listing")
    expect "suite: every y_ accepted, every n_ refused, every i_ as listed" \
        "y_: 95 of 95 accepted; n_: 188 of 188 refused; i_: 35 of 35 as listed" "$summary"
    sort -k2 "$work/listing" | sed "s/^/      /"
fi

# Fuzzing under AddressSanitizer and UBSan: many more keys for the damaged documents, the random bytes and the
# written documents.
capture smith build -DMODE=asan
for fuzz in json_damaged_documents_never_misbehave json_random_bytes_never_misbehave json_any_value_written; do
    capture ./build/tests --test $fuzz --seeds 5000
    summary=$(printf '%s\n' "$out" | grep ' passed, ' | sed 's/ in [0-9.]* s$//')
    expect "fuzz: $fuzz on 5000 keys under ASan and UBSan" "1 passed, 0 failed" "$summary"
done

# The library alone: it compiles in C23 and C2y, and with the checks off, using no library function of libc.
for std in gnu23 gnu2y; do
    capture $cc -std=$std -Wall -Wextra -Wpedantic -Wconversion -Werror -O2 -I "$here/include" -c "$here/lib/json.c" -o "$work/json.o"
    expect "library: compiles as $std" 0 "$rc"
done
capture $cc -std=gnu23 -O2 -DNDEBUG -ffreestanding -fno-stack-protector -I "$here/include" -c "$here/lib/json.c" -o "$work/json.o"
# smith's own libraries are not libc, and the compiler's calls for a big struct are not the program's
undefined=$(nm -u "$work/json.o" | tr -d ' ' | grep -v -x -e _memcpy -e _memset -e _bzero -e _memmove -e '_smith_.*' | sort | tr '\n' ' ')
expect "library: names no libc function" "" "$undefined"

echo "$total tests, $failures failed"
[ "$failures" -eq 0 ]
