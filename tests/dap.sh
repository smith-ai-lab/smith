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

# .smith names as written (issue #27): the debugger shows `geometry::box::area` and `geometry::box::Box`, never the C
# spelling `geometry_box_area`. A module's functions and variables are named so at the top; its types are `Box` in the
# namespaces `geometry` and `box`, so that an expression can name them. Skipped for a smith without the token hook.
if "$smith" cc --smith-which -x c 2>/dev/null | grep -q "token-hook yes"; then
    mkdir "$work/lang" "$work/lang/geometry"
    cd "$work/lang" || exit 1
    cat >geometry/box.smith <<'EOF'
module geometry::box;
export typedef struct { f32 w; f32 h; } Box;
export f32 area(Box b) {
  f32 r = b.w * b.h;
  return r;
}
EOF
    cat >main.smith <<'EOF'
module main;
import geometry::box;
int main(void) {
  box::Box b = {2, 3};
  f32 a = box::area(b);
  return (int)a - 6;
}
EOF
    mkdir include
    if "$smith" cc --smith-header geometry/box.smith -o include/box.h &&
        "$smith" cc -g -O0 -I include -c geometry/box.smith -o box.o &&
        "$smith" cc -g -O0 -I include -c main.smith -o main.o &&
        "$smith" cc -g box.o main.o -o lang; then
        out=$("$smith" dbg -b -o 'b geometry::box::area' -o run -o bt -o 'frame variable' -o 'print b.w' \
            -o 'print (geometry::box::Box)b' -o 'type lookup geometry::box::Box' -o finish lang 2>&1)
        expect_contains "dbg stops in the module's function" "$out" "stop reason = breakpoint 1.1"
        expect_contains "dbg's frame has the name as written" "$out" "frame #0: "
        expect_contains "dbg's frame is geometry::box::area" "$out" "lang\`geometry::box::area(b=(w = 2, h = 3)) at box.smith"
        expect_contains "dbg's backtrace goes through main.smith" "$out" "main at main.smith:5"
        expect_contains "dbg prints a parameter's type as written" "$out" "(geometry::box::Box) b = (w = 2, h = 3)"
        expect_contains "dbg reads a field" "$out" "(f32) 2"
        expect_contains "dbg casts to a type as written" "$out" "(geometry::box::Box)  (w = 2, h = 3)"
        expect_contains "dbg looks the type up by its name as written" "$out" "f32 w;"
        expect_contains "dbg's finish returns the value" "$out" "Return value: (f32) \$0 = 6"
        out=$("$smith" dbg -b -o 'b main.smith:5' -o run -o 'frame variable b' lang 2>&1)
        expect_contains "dbg's other compile unit agrees on the type" "$out" "(geometry::box::Box) b = (w = 2, h = 3)"
        case $out in
        *geometry_box*) total=$((total + 1)); failures=$((failures + 1)); echo "FAIL  the C spelling shows in dbg" ;;
        *) total=$((total + 1)); echo "ok    the C spelling does not show in dbg" ;;
        esac
        "$work/dap_client" "$smith" "$work/lang/lang" "$work/lang/geometry/box.smith" 5 r 0 >"$work/dap-lang.log" 2>&1
        status=$?
        log=$(cat "$work/dap-lang.log")
        total=$((total + 1))
        if [ "$status" -eq 0 ]; then
            echo "ok    dap: a .smith session"
        else
            failures=$((failures + 1))
            echo "FAIL  dap .smith session (status $status)"
            printf '%s\n' "$log" | cut -c1-300 | tail -n 30
        fi
        expect_contains "dap's frame has the name as written" "$log" '"name":"geometry::box::area'
        expect_contains "dap's variable has the type as written" "$log" '"type":"geometry::box::Box"'

        # A generic's instance (issue #15): the frame is `pick::larger<i32>` with the types it was asked for, and it is in the
        # generic's own source, at the line written there, not in the compiler's copy of the tokens.
        mkdir "$work/generic"
        cat >"$work/generic/pick.smith" <<'EOF'
module pick;

export T larger(constexpr type T, T a, T b) {
    T result = a > b ? a : b;
    return result;
}
EOF
        cat >"$work/generic/gmain.smith" <<'EOF'
module gmain;
import pick;
int main(void) {
    i32 a = 3;
    f64 d = 2.5;
    return pick::larger(a, 8) + (int)pick::larger(d, 1.5) - 10;
}
EOF
        if (cd "$work/generic" && "$smith" cc -g -O0 gmain.smith pick.smith -o generic); then
            out=$(cd "$work/generic" && "$smith" dbg -b -o 'br set -r larger' -o run -o bt -o 'frame variable' -o c -o bt generic 2>&1)
            expect_contains "dbg stops in an instance" "$out" "stop reason = breakpoint 1.1"
            expect_contains "dbg's frame is the instance, named with its types" "$out" "generic\`pick::larger<i32>(a=3, b=8) at pick.smith:4"
            expect_contains "dbg's other instance has its own types" "$out" "generic\`pick::larger<f64>(a=2.5, b=1.5) at pick.smith:4"
            expect_contains "dbg lists the generic's own source" "$out" "T result = a > b ? a : b;"
            expect_contains "dbg shows the parameter's type as written" "$out" "(i32) a = 3"
            case $out in
            *"checksum mismatch"* | *smith-instance*) total=$((total + 1)); failures=$((failures + 1)); echo "FAIL  dbg shows the compiler's copy of the tokens" ;;
            *) total=$((total + 1)); echo "ok    dbg shows no compiler-made file or warning for an instance" ;;
            esac
        else
            total=$((total + 1))
            failures=$((failures + 1))
            echo "FAIL  the generic program does not build"
        fi
    else
        total=$((total + 1))
        failures=$((failures + 1))
        echo "FAIL  the .smith program does not build"
    fi
    cd "$work/src" || exit 1
else
    echo "skipped: this smith has no token hook, so no .smith names"
fi

echo
echo "$total debugger checks, $failures failed"
[ "$failures" -eq 0 ]
