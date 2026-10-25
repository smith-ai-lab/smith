#!/bin/sh
# End-to-end tests for `smith dbg --crash` and `smith dbg --at` (runtime/explain.c, shim/lldb_explain.cpp): a program run
# under lldb whose state is printed at a crash or a line, in C and in .smith. They launch real processes under a
# debugger, so they need what tests/dap.sh needs and are skipped, not failed, without it.
# Usage: sh tests/dbg_crash.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
export XDG_DATA_HOME=$work/data
export XDG_CACHE_HOME=$work/cache
PATH=$(dirname "$smith"):$PATH
export PATH

failures=0
total=0

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

expect_equal() { # name, actual, expected
    total=$((total + 1))
    if [ "$2" = "$3" ]; then
        echo "ok    $1"
    else
        failures=$((failures + 1))
        echo "FAIL  $1"
        echo "  expected: $3"
        echo "  actual:   $2"
    fi
}

if [ "$("$smith" cc --smith-which -c a.c | head -n 1 | cut -d ' ' -f 1,2)" != "clang in-process" ]; then
    echo "skipped: this smith has no clang inside (the stage0)"
    exit 0
fi
if ! "$smith" dbg --version >/dev/null 2>&1; then
    echo "skipped: this smith has no lldb inside"
    exit 0
fi

mkdir "$work/src"
cd "$work/src" || exit 1
cat >prog.c <<'EOF'
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int x;
    int y;
} Point;

static int pick(Point p, int i) {
    int a[4] = {1, 2, 3, 4};
    int v = a[i & 3] + p.x;
    return v;
}

int main(int argc, char **argv) {
    Point p = {3, 4};
    printf("out\n");
    fprintf(stderr, "err\n");
    if (argc > 1 && argv[1][0] == 'n') return *(int *)(long)(argc - 2);
    if (argc > 1 && argv[1][0] == 'a') abort();
    if (argc > 1 && argv[1][0] == 'h') {
        int *heap = malloc(16);
        return heap[pick(p, 1) + 8];
    }
    if (argc > 1 && argv[1][0] == 'e') return 7;
    int total = 0;
    for (int i = 0; i < 3; i++)
        total += pick(p, i);
    return total == 0;
}
EOF
if ! "$smith" cc -g -O0 -c prog.c -o prog.o || ! "$smith" cc prog.o -o prog ||
    ! "$smith" cc -g -O0 -fsanitize=address -c prog.c -o prog_asan.o || ! "$smith" cc -fsanitize=address prog_asan.o -o prog_asan; then
    echo "FAIL  prog.c does not build"
    exit 1
fi

probe=$("$smith" dbg --crash ./prog n 2>&1)
case $probe in
*"not debugged"*)
    echo "skipped: this account may not debug (see knowledge/debugger.md): $probe"
    exit 0
    ;;
esac

# 1. A C program that writes past an array: ASan's report as the program printed it, then the frames with their values.
"$smith" dbg --crash ./prog_asan h >out.txt 2>err.txt
status=$?
err=$(cat err.txt)
expect_equal "asan: the status is 1" "$status" 1
expect_contains "asan: the report is the program's own" "$err" "ERROR: AddressSanitizer: heap-buffer-overflow"
expect_contains "asan: the stop reason" "$err" "smith dbg: stopped: Heap buffer overflow"
expect_contains "asan: the frame of main" "$err" "smith dbg: prog.c:23: main"
expect_contains "asan: the source line" "$err" "return heap[pick(p, 1) + 8];"
expect_contains "asan: a struct local" "$err" "@ p     = (x = 3, y = 4)"
case $err in
*compiler-rt*) expect_equal "asan: no frame of the sanitizer's runtime" "shown" "hidden" ;;
*) expect_equal "asan: no frame of the sanitizer's runtime" "hidden" "hidden" ;;
esac

# 2. A signal: the stop reason, the status 128 + 11, and the standard output and error as they are.
"$smith" dbg --crash ./prog n >out.txt 2>err.txt
status=$?
err=$(cat err.txt)
expect_equal "signal: the status is 128 + SIGSEGV" "$status" 139
expect_contains "signal: the stop reason" "$err" "smith dbg: stopped: EXC_BAD_ACCESS"
expect_contains "signal: the failing line" "$err" "smith dbg: prog.c:19: main"
expect_contains "signal: a local as @" "$err" "@ argc  = 2"
expect_contains "signal: the program's standard error passes through" "$err" "err"
expect_equal "signal: the program's standard output is not smith's" "$(grep -c 'smith dbg' out.txt)" 0

# 3. abort and --frames.
"$smith" dbg --crash --frames 1 ./prog a >out.txt 2>err.txt
status=$?
err=$(cat err.txt)
expect_equal "abort: the status is 128 + SIGABRT" "$status" 134
expect_contains "abort: the stop reason" "$err" "smith dbg: stopped: signal SIGABRT"
expect_equal "abort: --frames 1 shows one frame" "$(grep -c '^smith dbg: prog.c:' err.txt)" 1

# 4. A program that exits: its output as it was, exited N, and its status.
"$smith" dbg --crash ./prog e >out.txt 2>err.txt
status=$?
expect_equal "exit: the status is the program's" "$status" 7
expect_contains "exit: the program's output" "$(cat out.txt)" "out"
expect_contains "exit: exited 7" "$(cat err.txt)" "smith dbg: exited 7"

# 5. --at: the locals at the line for the first N hits, then the program is ended.
"$smith" dbg --at prog.c:11 --count 2 ./prog >out.txt 2>err.txt
status=$?
err=$(cat err.txt)
expect_equal "at: the status is 0" "$status" 0
expect_equal "at: two hits" "$(grep -c 'smith dbg: at prog.c:11, hit' err.txt)" 2
expect_contains "at: the first hit's argument" "$err" "@ i = 0"
expect_contains "at: the second hit's argument" "$err" "@ i = 1"
expect_equal "at: the program was ended before it printed its total" "$(grep -c 'exited' err.txt)" 0

# 6. A .smith program, in the project smith new writes: a slice out of range and a failed contract in debug.
mkdir "$work/proj"
cd "$work/proj" || exit 1
"$smith" new app >/dev/null 2>&1
cd app || exit 1
cat >main.smith <<'EOF'
module main;

import smith::contract;

i32 total(i32 xs[:], usize i) {
    i32 v = xs[i];
    return v;
}

i32 positive(i32 n) {
    contract::requires(n > 0);
    return n;
}

i32 main(i32 argc, char **) {
    i32 data[4] = {1, 2, 3, 4};
    i32 xs[:] = data[0:4];
    usize at = 7;
    if (argc > 2) return positive(-1);
    if (argc > 1) return total(xs, at);
    return total(xs, 1);
}
EOF
rm -f primes.smith
if ! "$smith" build >build.txt 2>&1; then
    echo "FAIL  the .smith program does not build"
    cat build.txt
    exit 1
fi
"$smith" dbg --crash build/debug/app x >out.txt 2>err.txt
status=$?
err=$(cat err.txt)
expect_equal "smith slice: the status is 128 + SIGABRT" "$status" 134
expect_contains "smith slice: the frame as written" "$err" "smith dbg: main.smith:6: main::total"
expect_contains "smith slice: the index" "$err" "@ i  = 7"
expect_contains "smith slice: the slice" "$err" "@ xs = count=4"
expect_contains "smith slice: the caller" "$err" "smith dbg: main.smith:20: main"
"$smith" dbg --crash build/debug/app x y >out.txt 2>err.txt
err=$(cat err.txt)
expect_contains "contract: the failing call's frame" "$err" "smith dbg: main.smith:11: main::positive"
expect_contains "contract: its argument" "$err" "@ n = -1"
expect_contains "contract: the caller's frame" "$err" "smith dbg: main.smith:19: main"
"$smith" dbg --crash build/debug/app >out.txt 2>err.txt
expect_contains "smith: a program that exits" "$(cat err.txt)" "smith dbg: exited"

# 7. A program that cannot be debugged is run plainly after one line.
"$smith" dbg --crash ./does-not-exist >out.txt 2>err.txt
expect_contains "refusal: one line saying why" "$(cat err.txt)" "smith dbg: not debugged:"

echo
echo "$total dbg --crash checks, $failures failed"
[ "$failures" -eq 0 ]
