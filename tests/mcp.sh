#!/bin/sh
# Tests of `smith mcp` (issue #22, docs/mcp.md), in two halves. Usage: sh tests/mcp.sh path/to/smith
#
# 1. The server on the fake io (tests/mcp_fake.c): a scripted client on the console and a scripted `smith dap` child, in
#    one process, built in three ways (-O0 under AddressSanitizer and UBSan, -O2, and -O2 -DNDEBUG). This half needs only
#    a C compiler and runs for the stage0 too.
# 2. The server as `smith mcp`, a real adapter and real programs, driven by lines on its standard input: a C program, a
#    .smith program (its names as written), and a test of a project launched by its name. It launches processes under a
#    debugger, so it needs a smith with lldb inside and the Developer Tools permission (docs/debugging.md); without
#    them this half is skipped with a message, not failed.
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
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

expect_equal() { # name, expected, actual
    total=$((total + 1))
    if [ "$2" = "$3" ]; then
        echo "ok    $1"
    else
        failures=$((failures + 1))
        echo "FAIL  $1"
        printf '  expected: %s\n  actual:   %s\n' "$2" "$3"
    fi
}

finish() {
    echo
    echo "$total mcp checks, $failures failed"
    [ "$failures" -eq 0 ]
    exit
}

# --- the server on the fake io -------------------------------------------------------------------------------------

compile="$smith cc -std=gnu23 -g -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror"
for mode in asan fast ndebug; do
    case $mode in
    asan) flags="-O0 -fsanitize=address,undefined -fno-sanitize-recover=all" ;;
    fast) flags="-O2" ;;
    ndebug) flags="-O2 -DNDEBUG" ;;
    esac
    # shellcheck disable=SC2086
    if ! $compile $flags "$here/tests/mcp_fake.c" -o "$work/mcp_fake_$mode" 2>"$work/build.log"; then
        total=$((total + 1))
        failures=$((failures + 1))
        echo "FAIL  fake io, $mode: tests/mcp_fake.c does not build"
        cat "$work/build.log"
        continue
    fi
    out=$("$work/mcp_fake_$mode" 2>&1)
    status=$?
    summary=$(printf '%s\n' "$out" | tail -n 1)
    case $summary in
    *" checks, 0 failed") expect_equal "fake io, $mode: $summary" 0 "$status" ;;
    *)
        total=$((total + 1))
        failures=$((failures + 1))
        echo "FAIL  fake io, $mode (status $status)"
        printf '%s\n' "$out" | head -n 40
        ;;
    esac
done

# --- the stage0 ------------------------------------------------------------------------------------------------------

if [ "$("$smith" cc --smith-which -c a.c | head -n 1 | cut -d ' ' -f 1,2)" != "clang in-process" ]; then
    out=$("$smith" mcp </dev/null 2>&1)
    status=$?
    expect_contains "the stage0 says smith mcp needs the full smith" "$out" "mcp needs the full smith"
    expect_equal "the stage0 exits with a failure" 1 "$((status != 0))"
    echo "skipped: this smith has no clang inside (the stage0), so no server to run"
    finish
fi

# --- the connection, with the real binary --------------------------------------------------------------------------------

# reply <out> <id>: the text of the tool result that answers the id, with its escapes undone and the work folder as DIR.
reply() {
    grep "^{\"id\":$2," "$1" | sed 's/.*"text":"//; s/","type":"text".*//; s/\\"/"/g' | sed "s|$work|DIR|g" |
        while IFS= read -r line; do printf '%b\n' "$line"; done
}

call() { # id, tool, arguments
    printf '{"jsonrpc":"2.0","id":%s,"method":"tools/call","params":{"name":"%s","arguments":%s}}\n' "$1" "$2" "$3"
}

printf '%s\n' \
    '{"jsonrpc":"2.0","id":1,"method":"server/discover"}' \
    '{"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"t","version":"1"}}}' \
    '{"jsonrpc":"2.0","method":"notifications/initialized"}' \
    '{"jsonrpc":"2.0","id":3,"method":"tools/list"}' >"$work/hello.jsonl"
"$smith" mcp <"$work/hello.jsonl" >"$work/hello.out" 2>"$work/hello.err"
expect_equal "smith mcp exits 0 when its input ends" 0 "$?"
expect_equal "smith mcp writes nothing to stderr" "" "$(cat "$work/hello.err")"
expect_equal "every line of stdout is a JSON message" 0 "$(grep -vc '^{"' "$work/hello.out")"
expect_equal "three answers for three requests" 3 "$(wc -l <"$work/hello.out" | tr -d ' ')"
expect_contains "server/discover supports 2026-07-28" "$(grep '^{"id":1,' "$work/hello.out")" '"supportedVersions":["2026-07-28"]'
expect_contains "initialize gives the version asked" "$(grep '^{"id":2,' "$work/hello.out")" '"protocolVersion":"2025-11-25"'
for tool in launch breakpoint continue step_over step_in step_out pause stack variables evaluate stop; do
    expect_contains "tools/list has $tool" "$(grep '^{"id":3,' "$work/hello.out")" "\"name\":\"$tool\""
done

# --- can this account debug? -----------------------------------------------------------------------------------------

mkdir "$work/c" && cd "$work/c" || exit 1
cat >prog.c <<'C'
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
C
if ! "$smith" cc -g -O0 prog.c -o prog; then
    echo "FAIL  prog.c does not build"
    exit 1
fi
if ! "$smith" dbg --version >/dev/null 2>&1; then
    echo "skipped: this smith has no lldb inside"
    finish
fi
probe=$("$smith" dbg -b -o 'b prog.c:10' -o run -o 'frame variable sum' prog 2>&1)
case $probe in
*"Developer Tools"* | *"not allowed"* | *"timed out"* | *"unable to attach"*)
    echo "skipped: this account may not debug (see docs/debugging.md): $probe"
    finish
    ;;
esac

# --- a C program ------------------------------------------------------------------------------------------------------

{
    call 1 launch '{"program":"prog","breakpoints":[{"file":"prog.c","line":10}]}'
    call 2 stack '{}'
    call 3 variables '{"frame":1,"depth":2}'
    call 4 evaluate '{"expression":"p.x + p.y"}'
    call 5 evaluate '{"expression":"sum = 9"}'
    call 6 breakpoint '{"file":"prog.c","line":17,"condition":"r == 7"}'
    call 7 step_out '{}'
    call 8 step_over '{}'
    call 9 continue '{}'
    call 10 stack '{}'
    call 11 continue '{}'
    call 12 continue '{}'
    call 13 stop '{}'
    call 14 stack '{}'
} >in.jsonl
"$smith" mcp <in.jsonl >out.jsonl 2>err.txt
expect_equal "a C session: nothing on stderr" "" "$(cat err.txt)"
expect_equal "a C session: every line of stdout is a JSON message" 0 "$(grep -vc '^{"' out.jsonl)"
expect_equal "launch runs to the breakpoint" \
    "session s1: DIR/c/prog
breakpoint 1: prog.c:10 (verified)
stopped: breakpoint (breakpoint 1)
  #0 add prog.c:10
  > 10:     return sum;" "$(reply out.jsonl 1)"
expect_contains "stack has the caller" "$(reply out.jsonl 2)" "#0 add prog.c:10
#1 main prog.c:15"
expect_equal "variables of the caller's frame, with the struct opened" \
    "frame #1, scope locals
p (Point)
  x = 3 (int)
  y = 4 (int)
r = 1 (int)" "$(reply out.jsonl 3 | sed 's/^r = [0-9-]* (int)/r = 1 (int)/')"
expect_equal "evaluate reads" "p.x + p.y = 7 (int)" "$(reply out.jsonl 4)"
expect_contains "an assignment is refused" "$(reply out.jsonl 5)" "refused: the expression may change the program"
expect_equal "a conditional breakpoint" "breakpoint 2: prog.c:17 if r == 7 (verified)" "$(reply out.jsonl 6)"
expect_equal "step_out returns to the caller's line" "stopped: step
  #0 main prog.c:15
  > 15:     int r = add(p);" "$(reply out.jsonl 7)"
expect_contains "step_over moves to the next line" "$(reply out.jsonl 8)" "stopped: step
  #0 main prog.c:16"
expect_contains "continue runs to the breakpoint, with the program's output" "$(reply out.jsonl 9)" "[stdout]
7
stopped: breakpoint (breakpoint 2)
  #0 main prog.c:17"
expect_contains "the stack at the second breakpoint" "$(reply out.jsonl 10)" "#0 main prog.c:17"
expect_contains "continue ends the program" "$(reply out.jsonl 11)" "exited: the program exited with code 0"
expect_contains "a continue after the end" "$(reply out.jsonl 12)" "the program has exited"
expect_equal "stop" "session s1 stopped (the program had exited with code 0)" "$(reply out.jsonl 13)"
expect_equal "the session is gone" "no debug session: call launch first" "$(reply out.jsonl 14)"

# A client that goes away while the program is stopped takes the program with it.
cat >spin.c <<'C'
#include <stdio.h>
#include <unistd.h>

int main(void) {
    FILE *file = fopen("spin.pid", "w");
    fprintf(file, "%d\n", (int)getpid());
    fclose(file);
    int stopped_here = 1;
    sleep(60);
    return stopped_here;
}
C
"$smith" cc -g -O0 spin.c -o spin
call 1 launch '{"program":"spin","breakpoints":[{"file":"spin.c","line":9}]}' >gone.jsonl
"$smith" mcp <gone.jsonl >gone.out 2>&1
expect_contains "a launch stopped before sleep" "$(reply gone.out 1)" "stopped: breakpoint"
pid=$(cat spin.pid 2>/dev/null || echo 0)
sleep 1
if kill -0 "$pid" 2>/dev/null; then
    kill -9 "$pid" 2>/dev/null
    expect_equal "the program is gone when the client's input ends" gone "still running"
else
    expect_equal "the program is gone when the client's input ends" gone gone
fi

# --- a .smith program (names as written) ----------------------------------------------------------------------------

if "$smith" cc --smith-which -x c 2>/dev/null | grep -q "token-hook yes"; then
    mkdir "$work/lang" "$work/lang/geometry" "$work/lang/include"
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
    if "$smith" cc --smith-header geometry/box.smith -o include/box.h &&
        "$smith" cc -g -O0 -I include -c geometry/box.smith -o box.o &&
        "$smith" cc -g -O0 -I include -c main.smith -o main.o && "$smith" cc -g box.o main.o -o lang; then
        {
            call 1 launch '{"program":"lang","breakpoints":[{"file":"geometry/box.smith","line":4}]}'
            call 2 stack '{}'
            call 3 variables '{"depth":1}'
            call 4 evaluate '{"expression":"b.w * b.h"}'
            call 5 continue '{}'
        } >in.jsonl
        "$smith" mcp <in.jsonl >out.jsonl 2>err.txt
        expect_equal "a .smith session: nothing on stderr" "" "$(cat err.txt)"
        expect_equal "launch stops in the module's function, named as written" \
            "session s1: DIR/lang/lang
breakpoint 1: geometry/box.smith:4 (verified)
stopped: breakpoint (breakpoint 1)
  #0 geometry::box::area geometry/box.smith:4
  > 4:   f32 r = b.w * b.h;" "$(reply out.jsonl 1)"
        expect_contains "the stack goes through main.smith" "$(reply out.jsonl 2)" "#1 main main.smith:5"
        expect_equal "the type is named as written" "frame #0, scope locals
b (geometry::box::Box)
  w = 2 (f32)
  h = 3 (f32)
r = 0 (f32)" "$(reply out.jsonl 3 | sed 's/^r = [0-9.e-]* (f32)/r = 0 (f32)/')"
        expect_equal "evaluate in a module" "b.w * b.h = 6 (f32)" "$(reply out.jsonl 4)"
        case $(cat out.jsonl) in
        *geometry_box*)
            total=$((total + 1))
            failures=$((failures + 1))
            echo "FAIL  the C spelling shows in the results"
            ;;
        *)
            total=$((total + 1))
            echo "ok    the C spelling does not show in the results"
            ;;
        esac
    else
        total=$((total + 1))
        failures=$((failures + 1))
        echo "FAIL  the .smith program does not build"
    fi
else
    echo "skipped: this smith has no token hook, so no .smith names"
fi

# --- a test of a project, launched by its name ---------------------------------------------------------------------

mkdir -p "$work/bin" && ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" &&
    ln -s "$smith" "$work/bin/smith-c++"
mkdir -p "$work/project/tests" && cd "$work/project" || exit 1
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithTests tests = {
        .sources = smith_list(b, "tests/test_a.c", nullptr),
        .flags = smith_list(b, "-std=gnu23", "-g", "-O0", "-I", ".smith/include", nullptr),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
C
cat >tests/test_a.c <<'C'
#include <smith/test.h>

static int twice(int x) {
    int doubled = x * 2;
    return doubled;
}

void test_twice_doubles(void) {
    int result = twice(21);
    SMITH_CHECK(result, ==, 42);
}
C
if env PATH="$work/bin:$PATH" CC=smith-cc CXX=smith-c++ smith build >"$work/project.log" 2>&1; then
    {
        call 1 launch '{"test":"twice_doubles","breakpoints":[{"file":"tests/test_a.c","line":5}]}'
        call 2 variables '{}'
        call 3 stack '{"levels":2}'
        call 4 step_out '{}'
        call 5 continue '{}'
        call 6 launch '{"test":"twice_doubles","runner":"build/nonesuch"}'
    } >in.jsonl
    "$smith" mcp <in.jsonl >out.jsonl 2>err.txt
    expect_equal "a test session: nothing on stderr" "" "$(cat err.txt)"
    expect_equal "launch by the name of a test stops in the test" \
        "session s1: DIR/project/build/tests --test twice_doubles --in-process
breakpoint 1: tests/test_a.c:5 (verified)
stopped: breakpoint (breakpoint 1)
  #0 twice tests/test_a.c:5
  > 5:     return doubled;" "$(reply out.jsonl 1)"
    expect_equal "the test's variables" "frame #0, scope locals
x = 21 (int)
doubled = 42 (int)" "$(reply out.jsonl 2)"
    expect_contains "the stack of the test" "$(reply out.jsonl 3)" "#0 twice tests/test_a.c:5
#1 test_twice_doubles tests/test_a.c:9"
    expect_contains "step_out returns to the test" "$(reply out.jsonl 4)" "#0 test_twice_doubles tests/test_a.c:9"
    expect_contains "the test passes" "$(reply out.jsonl 5)" "1 passed, 0 failed"
    expect_contains "the program ends with code 0" "$(reply out.jsonl 5)" "exited: the program exited with code 0"
    expect_contains "a runner that is not there" "$(reply out.jsonl 6)" "build/nonesuch is not a file"
else
    total=$((total + 1))
    failures=$((failures + 1))
    echo "FAIL  the scratch project does not build"
    cat "$work/project.log"
fi

finish
