#!/bin/sh
# Tests of <smith/random.h>, with real clang, ninja and clangd. Usage: sh tests/random.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
cc=${CC:-cc}
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
        echo "  expected: $2"
        echo "  actual:   $3"
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

# The same bits everywhere: the digest of a large batch of draws, which is part of the API, in every configuration.
expected_digest=4a2e897669d609ac
warn="-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wimplicit-fallthrough -Wmissing-prototypes -Wstrict-prototypes -Wvla -Werror"
sources="$here/tests/random.c $here/lib/arena.c"

# 1. The checks, in two builds: -O0 under AddressSanitizer and UBSan, and -O2.
$cc -std=gnu23 $warn -I "$here/include" -O0 -g -fsanitize=address,undefined $sources -o "$work/asan" || exit 1
capture "$work/asan"
expect "checks: pass at -O0 under ASan and UBSan" "0 random: all checks passed" "$rc $out"
$cc -std=gnu23 $warn -I "$here/include" -O2 $sources -o "$work/fast" || exit 1
capture "$work/fast"
expect "checks: pass at -O2" "0 random: all checks passed" "$rc $out"
capture "$work/fast" stats
echo "$out" | sed 's/^/      /'

# 2. The library compiles as -std=gnu23 and -std=gnu2y with the warnings of smith new.
for std in gnu23 gnu2y; do
    capture $cc -std=$std $warn -I "$here/include" -c "$here/lib/random.c" -o "$work/r.o"
    expect "library: compiles as -std=$std with the new project's warnings" 0 "$rc"
done

# 3. Digests: every configuration prints the same one.
digest() { # name, flags...
    name=$1
    shift
    if $cc -I "$here/include" -w "$@" $sources -o "$work/d" 2>"$work/derr"; then
        capture "$work/d" digest
        expect "digest: $name" "$expected_digest" "$out"
    else
        total=$((total + 1))
        failures=$((failures + 1))
        echo "FAIL  digest: $name did not build"
        cat "$work/derr"
    fi
}
digest "-O0 sanitized" -std=gnu23 -O0 -g -fsanitize=address,undefined
digest "-O2" -std=gnu23 -O2
digest "-O2 -DNDEBUG" -std=gnu23 -O2 -DNDEBUG
digest "-O3 -DNDEBUG" -std=gnu23 -O3 -DNDEBUG
digest "-std=gnu2y" -std=gnu2y -O2
digest "-ffp-contract=fast, which the pragma defeats" -std=gnu23 -O2 -ffp-contract=fast
digest "-O3 -march=native -ffp-contract=fast" -std=gnu23 -O3 -march=native -ffp-contract=fast
# 4. The refusals.
capture $cc -std=gnu23 -I "$here/include" -ffast-math -c "$here/lib/random.c" -o "$work/r.o"
expect "refusal: -ffast-math fails to compile" yes "$([ "$rc" -ne 0 ] && echo yes)"
expect_contains "refusal: ... with the reason" "$err" "-ffast-math changes floating-point results"
capture $cc -std=gnu23 -I "$here/include" -Ofast -c "$here/lib/random.c" -o "$work/r.o"
expect "refusal: -Ofast fails to compile" yes "$([ "$rc" -ne 0 ] && echo yes)"
capture "$work/fast" assert
expect "refusal: a normal with deviation 0 aborts" 134 "$rc"
expect_contains "refusal: ... naming the assertion" "$err" "normal.deviation > 0"

# 5. A project gets random and the arena with smith_library.
mkdir "$work/project" && cd "$work/project" || exit 1
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu23", "-Wall", "-Werror", "-fsanitize=address", "-I", ".smith/include", nullptr);
    SmithObject main = {.compiler = "smith-cc", .source = "main.c", .output = "build/main.o", .flags = flags};
    smith_object(b, &main);
    SmithObject random = {.compiler = "smith-cc", .source = smith_library(b, "random"), .output = "build/smith/random.o", .flags = flags};
    smith_object(b, &random);
    SmithObject arena = {.compiler = "smith-cc", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
    smith_object(b, &arena);
    SmithProgram program = {
        .linker = "smith-cc",
        .output = "build/app",
        .flags = smith_list(b, "-fsanitize=address", nullptr),
        .objects = (const SmithObject *[]){&main, &random, &arena, nullptr},
    };
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
    smith_compile_commands(b, "compile_commands.json");
}
C
cat >main.c <<'C'
#include <smith/arena.h>
#include <smith/random.h>

#include <stdio.h>

// Your own distribution, written like smith's: built only from the public primitives.
static double exponential_sample(SmithKey key, double rate) {
    return -smith_log_double(1.0 - smith_key_unit_double(key, 0)) / rate;
}

int main(void) {
    SmithKey key = smith_key_make(42);
    double mean = 0;
    for (uint64_t i = 0; i < 1000; i++)
        mean += exponential_sample(smith_key_derive(key, i), 2.0) / 1000.0;
    float x = smith_normal_float_sample(smith_key_derive(key, 5000), (SmithNormalFloat){.mean = 0, .deviation = 1});
    alignas(smith_arena_align_max) static unsigned char memory[1024];
    SmithArena arena = smith_arena_make(memory, sizeof memory);
    SmithCategorical categorical = smith_categorical_make(&arena, (const double[]){1, 1, 2}, 3);
    printf("mean %.1f, normal %d, index %zu\n", mean, x > -6.0f && x < 6.0f, smith_categorical_sample(key, categorical) < 3 ? (size_t)1 : (size_t)0);
    return 0;
}
C
mkdir "$work/bin" && ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" && ln -s "$smith" "$work/bin/smith-c++"
PATH="$work/bin:$PATH"
capture "$smith" build
expect "project: the build succeeds" 0 "$rc"
expect "project: the header is written" yes "$([ -f .smith/include/smith/random.h ] && echo yes)"
expect "project: the source is written" yes "$([ -f .smith/lib/random.c ] && echo yes)"
capture build/app
expect "project: the program runs" "0 mean 0.5, normal 1, index 1" "$rc $out"
before=$(stat -f %m .smith/lib/random.c 2>/dev/null || stat -c %Y .smith/lib/random.c)
sleep 1
capture "$smith" build
after=$(stat -f %m .smith/lib/random.c 2>/dev/null || stat -c %Y .smith/lib/random.c)
expect "project: an unchanged library is not rewritten" "$before" "$after"
if "$smith" lsp --version >/dev/null 2>&1; then
    capture "$smith" lsp --check=main.c
    errors=$(printf '%s\n%s\n' "$out" "$err" | grep -c 'E\[')
    expect "project: clangd reports no errors in main.c" 0 "$errors"
fi

echo
echo "$total random checks, $failures failed"
[ "$failures" -eq 0 ]
