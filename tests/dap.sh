#!/bin/sh
# End-to-end tests for `smith dbg` (lldb's driver) and `smith dap` (lldb-dap), both run in smith's own process from the
# LLVM it links, with the system's debugserver. A smith without them inside (the stage0) is skipped. They launch real
# processes under a debugger, so they need Developer mode and the Developer Tools permission of the terminal running
# them (docs/debugging.md); without it the tests are skipped with a message, not failed.
# Usage: sh tests/dap.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
export XDG_DATA_HOME=$work/data
export XDG_CACHE_HOME=$work/cache

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

typedef struct {
    int x;
    int y;
} Point;

static int add(Point p) {
    int sum = p.x + p.y;
    return sum;
}

int main(void) {
    Point p = {3, 4};
    int r = add(p);
    printf("%d\n", r);
    return r == 7 ? 0 : 1;
}
EOF
if ! "$smith" cc -g -O0 prog.c -o prog; then
    echo "FAIL  prog.c does not build"
    exit 1
fi

# smith dbg, in batch mode: a breakpoint by line, run, a variable, a struct field, a backtrace.
out=$("$smith" dbg -b -o 'b prog.c:10' -o run -o 'frame variable sum' -o 'frame variable p' -o 'print p.x' -o bt prog 2>&1)
case $out in
*"Developer Tools"* | *"not allowed"* | *"timed out"* | *"unable to attach"*)
    echo "skipped: this account may not debug (see docs/debugging.md): $out"
    exit 0
    ;;
esac
expect_contains "dbg stops at the breakpoint" "$out" "stop reason = breakpoint 1.1"
expect_contains "dbg prints a local" "$out" "(int) sum = 7"
expect_contains "dbg prints a struct" "$out" "x = 3"
expect_contains "dbg prints a field" "$out" "(int) 3"
expect_contains "dbg prints a backtrace" "$out" "frame #0: "
expect_contains "dbg's backtrace has the caller" "$out" "main at prog.c"

# smith dap: a client writes DAP messages, and the adapter's replies are read back.
if ! "$smith" cc -std=gnu23 "$here/tests/dap_client.c" -o "$work/dap_client"; then
    echo "FAIL  tests/dap_client.c does not build"
    exit 1
fi
"$work/dap_client" "$smith" "$work/src/prog" "$work/src/prog.c" 10 sum 7 >"$work/dap.log" 2>&1
status=$?
log=$(cat "$work/dap.log")
total=$((total + 1))
if [ "$status" -eq 0 ]; then
    echo "ok    dap: initialize, launch, setBreakpoints, configurationDone, stopped, stackTrace, scopes, variables, continue, terminated"
else
    failures=$((failures + 1))
    echo "FAIL  dap session (status $status)"
    printf '%s\n' "$log" | cut -c1-300 | tail -n 30
fi
expect_contains "dap reports the stop at the breakpoint" "$log" '"reason":"breakpoint"'
expect_contains "dap lists the frame in add" "$log" '"name":"add'
expect_contains "dap lists the fields of the struct" "$log" '"name":"x"'
expect_contains "dap reports the process ended" "$log" '"event":"terminated"'

echo
echo "$total debugger checks, $failures failed"
[ "$failures" -eq 0 ]
