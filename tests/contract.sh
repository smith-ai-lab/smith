#!/bin/sh
# Tests of <smith/contract.h>, with real clang and ninja. Usage: sh tests/contract.sh path/to/smith
# The macros' run-time behaviour (the message, the stop, NDEBUG), the calls they leave in the IR for `smith prove` to
# find, and the library built by a project with smith_library. lib/smith/contract_test.smith runs under smith's own test library.
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
cc="$smith cc"
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
    fi
}

expect_contains() { # name, text, needle
    total=$((total + 1))
    case $2 in
    *"$3"*) echo "ok    $1" ;;
    *)
        failures=$((failures + 1))
        echo "FAIL  $1"
        printf '  expected to contain: %s\n' "$3"
        printf '  actual:              %s\n' "$2"
        ;;
    esac
}

flags="-std=gnu23 -Wall -Wextra -Wpedantic -Wconversion -Werror -I $here/include"

# A program with one contract of each kind; its arguments pick which one fails.
cat >"$work/clamp.c" <<'C'
#include <smith/contract.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int32_t clamp(int32_t x, int32_t low, int32_t high) {
    smith_requires(low <= high);
    const int32_t old = x; // kept only for the contract below
    int32_t result = x < low ? low : x > high ? high : x;
    smith_ensures(low <= result && result <= high && (result == old || result == low || result == high));
    return result;
}

static int32_t twice(int32_t count) {
    int32_t total = 0;
    for (int32_t i = 0; i < count; i++) {
        smith_invariant(total == i * 2);
        total += 2;
    }
    return total;
}

int main(int argc, char **argv) {
    (void)argc;
    int32_t x = atoi(argv[1]);
    int32_t low = atoi(argv[2]);
    int32_t high = atoi(argv[3]);
    printf("%d %d\n", clamp(x, low, high), twice(high));
    return 0;
}
C
cat >"$work/ensures.c" <<'C'
#include <smith/contract.h>

#include <stdio.h>

static int wrong(int x) {
    int result = x + 1;
    smith_ensures(result == x);
    return result;
}

int main(void) {
    printf("%d\n", wrong(1));
    return 0;
}
C
cat >"$work/invariant.c" <<'C'
#include <smith/contract.h>

int main(void) {
    int total = 0;
    for (int i = 0; i < 4; i++) {
        smith_invariant(total == i * 2);
        total += 2;
        if (i == 1) total++;
    }
    return total;
}
C

# 1. At run time: passing contracts are silent, a failing one prints its text, file and line, then aborts.
$cc $flags "$work/clamp.c" "$here/lib/smith/contract.smith" "$here/lib/trace.c" -o "$work/clamp" || exit 1
capture "$work/clamp" 5 1 3
expect "run: true contracts pass" "0 3 6" "$rc $out"
capture "$work/clamp" 5 3 1
expect "run: a false smith_requires aborts" 134 "$rc"
expect_contains "run: ... and says which contract failed" "$err" "smith-trace begin requires failed"
expect "run: ... before anything else is printed" "" "$out"
$cc $flags "$work/ensures.c" "$here/lib/smith/contract.smith" "$here/lib/trace.c" -o "$work/ensures" || exit 1
capture "$work/ensures"
expect "run: a false smith_ensures aborts" 134 "$rc"
expect_contains "run: ... and says which contract failed" "$err" "smith-trace begin ensures failed"
$cc $flags "$work/invariant.c" "$here/lib/smith/contract.smith" "$here/lib/trace.c" -o "$work/invariant" || exit 1
capture "$work/invariant"
expect "run: a false smith_invariant aborts" 134 "$rc"
expect_contains "run: ... and says which contract failed" "$err" "smith-trace begin invariant failed"

# 2. The same program under the sanitizers, and optimized: the checks are calls, so no optimization drops them.
$cc $flags -g -fsanitize=address,undefined "$work/clamp.c" "$here/lib/smith/contract.smith" "$here/lib/trace.c" -o "$work/clamp_asan" || exit 1
capture "$work/clamp_asan" 5 1 3
expect "sanitized: true contracts pass" "0 3 6" "$rc $out"
$cc $flags -O2 "$work/clamp.c" "$here/lib/smith/contract.smith" "$here/lib/trace.c" -o "$work/clamp_fast" || exit 1
capture "$work/clamp_fast" 5 3 1
expect "optimized: a false smith_requires still aborts" 134 "$rc"

# 3. NDEBUG compiles the checks away: no call, no evaluation, no unused-variable warning for an old value.
capture $cc $flags -DNDEBUG -c "$work/clamp.c" -o "$work/clamp_ndebug.o"
expect "NDEBUG: -Wall -Wextra -Werror compile, though old is used only in a contract" 0 "$rc"
undefined=$(nm -u "$work/clamp_ndebug.o" | grep -c smith_contract_)
expect "NDEBUG: the object calls no contract function" 0 "$undefined"
$cc $flags -DNDEBUG "$work/clamp.c" -o "$work/clamp_ndebug" || exit 1
capture "$work/clamp_ndebug" 5 3 1
expect "NDEBUG: a false contract is not checked" "0 1 2" "$rc $out"
cat >"$work/effect.c" <<'C'
#include <smith/contract.h>

#include <stdint.h>

int32_t checked(int32_t x) {
    smith_requires(x > 0);
    int32_t y = x * 2;
    smith_ensures(y > x);
    for (int32_t i = 0; i < 2; i++) smith_invariant(i < 2);
    return y;
}
C
$cc $flags -DNDEBUG -O2 -c "$work/effect.c" -o "$work/effect.o" || exit 1
undefined=$(nm -u "$work/effect.o" | grep -c smith_contract_)
expect "NDEBUG: the optimized object calls no contract function" 0 "$undefined"
$cc $flags -DNDEBUG -S -emit-llvm -O2 "$work/effect.c" -o "$work/effect.ll" || exit 1
expect "NDEBUG: the optimizer leaves nothing of the arguments" 0 "$(grep -c 'icmp' "$work/effect.ll")"

# 4. In the IR, with no flag: a call per contract that smith prove finds by name, at -O0 and optimized.
for level in -O0 -O2; do
    $cc $flags $level -S -emit-llvm "$work/clamp.c" -o "$work/clamp.ll" || exit 1
    ir=$(cat "$work/clamp.ll")
    expect_contains "IR $level: smith_requires is a call" "$ir" "call void @smith_contract_requires(i1 noundef zeroext"
    expect_contains "IR $level: smith_ensures is a call" "$ir" "call void @smith_contract_ensures(i1"
    expect_contains "IR $level: smith_invariant is a call" "$ir" "call void @smith_contract_invariant(i1"
done
$cc $flags -DNDEBUG -S -emit-llvm "$work/clamp.c" -o "$work/clamp.ll" || exit 1
calls=$(grep -c 'call void @smith_contract_' "$work/clamp.ll")
expect "IR NDEBUG: no contract call" 0 "$calls"

# 5. The library alone compiles as C23 and C2y.
for std in gnu23 gnu2y; do
    capture $cc -std=$std -Wall -Wextra -Wpedantic -Wconversion -Werror -O2 -I "$here/include" -c "$here/lib/smith/contract.smith" -o "$work/contract.o"
    expect "library: compiles as $std" 0 "$rc"
done

# 6. A project gets the library with smith_library, and its tests run under smith's own test library.
mkdir -p "$work/bin" && ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" && ln -s "$smith" "$work/bin/smith-c++"
PATH="$work/bin:$PATH"
mkdir -p "$work/project/smith" && cd "$work/project" || exit 1
cp "$here/lib/smith/contract_test.smith" smith/contract_test.smith
cat >build.c <<'C'
#include <smith/build.h>

#include <stddef.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu2y", "-g", "-O0", "-Wall", "-Wextra", "-Wpedantic", "-Wshadow", "-Wconversion", "-Werror", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-I", ".smith/include", nullptr);
    const char *const *names = smith_list(b, "arena", "contract", "random", nullptr);
    SmithObject objects[3] = {};
    for (size_t i = 0; names[i] != nullptr; i++) {
        objects[i] = (SmithObject){.compiler = "smith-cc", .source = smith_library(b, names[i]),
                                   .output = smith_fmt(b, "build/smith/%s.o", names[i]), .flags = flags};
        smith_object(b, &objects[i]);
    }
    SmithTests tests = {
        .sources = smith_list(b, "smith/contract_test.smith", nullptr),
        .objects = (const SmithObject *[]){&objects[0], &objects[1], &objects[2], nullptr},
        .flags = flags,
        .link_flags = smith_list(b, "-fsanitize=address,undefined", nullptr),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
C
capture smith build
[ "$rc" -eq 0 ] || printf '%s\n%s\n' "$out" "$err"
expect "project: the tests build with smith_library" 0 "$rc"
capture ./build/tests
summary=$(printf '%s\n' "$out" | grep ' passed, ' | sed 's/ in [0-9.]* s$//')
count=$(grep -c '^export void test_' "$here/lib/smith/contract_test.smith")
expect "project: all $count tests pass" "$count passed, 0 failed" "$summary"
expect "project: the header is written" yes "$([ -f .smith/include/smith/contract.h ] && echo yes)"

# smith_tests links the contract library itself, so a project that does not list it (the code under test makes
# contracts and the project never wired the library) still links.
sed -e 's/"arena", "contract", "random"/"arena", "random"/' -e 's/objects\[3\]/objects[2]/' \
    -e 's/&objects\[2\], nullptr/nullptr/' build.c >build.nocontract.c
grep -q '"arena", "random"' build.nocontract.c || { echo "FAIL  the sed of build.c did not apply"; failures=$((failures + 1)); }
mv build.nocontract.c build.c
capture smith build clean
capture smith build
[ "$rc" -eq 0 ] || printf '%s\n%s\n' "$out" "$err"
expect "project: smith_tests links the contract library when .objects leaves it out" 0 "$rc"
capture ./build/tests
summary=$(printf '%s\n' "$out" | grep ' passed, ' | sed 's/ in [0-9.]* s$//')
expect "project: ... and the tests of code with contracts pass" "$count passed, 0 failed" "$summary"

# The static analyzer (clang-tidy's clang-analyzer-*) takes a contract as an assumption: an index a smith_requires
# bounds is not reported, with and without NDEBUG, and an index nothing bounds still is.
cat >"$work/bounded.c" <<'C'
#include <smith/contract.h>

#include <stddef.h>
#include <stdint.h>

static int32_t cells[8];

int32_t read_cell(size_t index) {
    smith_requires(index < 8);
    return cells[index];
}

void write_cell(size_t index, int32_t value) {
    smith_requires(index < 8);
    cells[index] = value;
}

int32_t read_past_the_end(void) {
    size_t index = 8;
    return cells[index];
}
C
if "$smith" lint --version >/dev/null 2>&1; then
    for mode in "" -DNDEBUG; do
        capture "$smith" lint --checks='-*,clang-analyzer-*' "$work/bounded.c" -- -std=gnu2y -I "$here/include" $mode
        found=$(printf '%s\n' "$out" | grep 'ArrayBound' | sed -e 's/^[^:]*:\([0-9]*\):.*/\1/' | tr '\n' ' ')
        expect "analyzer${mode:+ $mode}: only the unbounded index is reported" "20 " "$found"
    done
fi

echo
echo "$total contract checks, $failures failed"
[ "$failures" -eq 0 ]
