#!/bin/sh
# End-to-end tests for `smith build`, with real ninja and the real clang smith picks. Each case runs in its own
# temporary project. Usage: sh tests/build.sh build/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")" && pwd)
cc=${CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM

failures=0
total=0
cases=0
out=
err=
rc=0

# Runs a command with its stdout in $out, its stderr in $err and its exit status in $rc.
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

expect_status() { # name, expected status (or "fail" for any non-zero)
    if [ "$2" = fail ]; then
        if [ "$rc" -ne 0 ]; then expect "$1" ok ok; else expect "$1" "non-zero status" "$rc"; fi
    else
        expect "$1" "$2" "$rc"
    fi
}

# A fresh project folder; the caller's build.c is written next.
project() {
    cases=$((cases + 1))
    mkdir "$work/$cases"
    cd "$work/$cases" || exit 1
}

lines() { # file: the number of lines, 0 when absent
    if [ -f "$1" ]; then wc -l <"$1" | tr -d ' '; else echo 0; fi
}

build() {
    capture "$smith" build "$@"
}

# 1. A copy step.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep copy = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = (const char *[]){"input", nullptr},
        .outputs = (const char *[]){"output", nullptr},
    };
    smith_step(b, &copy);
    smith_default(b, copy.outputs);
}
C
echo one >input
build
expect "copy: the first build exits 0" 0 "$rc"
expect "copy: the first build makes the output" one "$(cat output 2>/dev/null)"
build
expect_contains "copy: the second build does nothing" "$out" "no work to do"
echo two >input
build
expect "copy: a changed input reruns the step" two "$(cat output)"
copy_project=$(pwd)

# 2. Tasks and stamp steps.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep hello = {.command = (const char *[]){"sh", "-c", "echo ran >> log", nullptr}};
    smith_task(b, "hello", &hello);
    SmithStep stamped = {
        .command = (const char *[]){"sh", "-c", "echo ran >> stamped_log", nullptr},
        .inputs = (const char *[]){"watched", nullptr},
    };
    smith_step(b, &stamped);
    smith_default(b, stamped.outputs);
}
C
echo a >watched
build hello
build hello
expect "task: runs on every smith build <task>" 2 "$(lines log)"
build
build
expect "stamp step: runs once for an unchanged input" 1 "$(lines stamped_log)"
echo b >watched
build
expect "stamp step: reruns when its input changes" 2 "$(lines stamped_log)"

# Two stamp steps with one command and different inputs get two stamps, so ninja accepts both.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep first = {
        .command = (const char *[]){"sh", "-c", "echo ran >> log", nullptr},
        .inputs = (const char *[]){"one", nullptr},
    };
    smith_step(b, &first);
    SmithStep second = {
        .command = (const char *[]){"sh", "-c", "echo ran >> log", nullptr},
        .inputs = (const char *[]){"two", nullptr},
    };
    smith_step(b, &second);
    smith_default(b, smith_cat(b, first.outputs, second.outputs, nullptr));
}
C
echo a >one
echo a >two
build
expect "stamp steps with one command: both run" "0 2" "$rc $(lines log)"

# 3. Spaces and a literal $.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep copy = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = (const char *[]){"my input.txt", nullptr},
        .outputs = (const char *[]){"my copy.txt", nullptr},
    };
    smith_step(b, &copy);
    SmithStep dollar = {
        .command = (const char *[]){"sh", "-c", "printf '%s\\n' \"$0\" > \"$1\"", "a$b 'q'", "$out", nullptr},
        .outputs = (const char *[]){"dollar file.txt", nullptr},
    };
    smith_step(b, &dollar);
    smith_default(b, smith_cat(b, copy.outputs, dollar.outputs, nullptr));
}
C
echo spaced >"my input.txt"
build
expect "spaces: a file name with a space is copied" spaced "$(cat 'my copy.txt' 2>/dev/null)"
expect "dollar: a literal \$ and quotes in a command word survive" "a\$b 'q'" "$(cat 'dollar file.txt' 2>/dev/null)"

# 4. A depfile.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep object = {
        .command = (const char *[]){"sh", "-c", "cc -MMD -MF a.o.d -c a.c -o a.o && echo x >> count", nullptr},
        .inputs = (const char *[]){"a.c", nullptr},
        .outputs = (const char *[]){"a.o", nullptr},
        .depfile = "a.o.d",
    };
    smith_step(b, &object);
    smith_default(b, object.outputs);
}
C
printf '#include "a.h"\nint a(void) { return VALUE; }\n' >a.c
echo '#define VALUE 1' >a.h
build
build
expect "depfile: an unchanged tree compiles once" 1 "$(lines count)"
echo '#define VALUE 2' >a.h
build
expect "depfile: a changed header rebuilds the object" 2 "$(lines count)"

# 5. Editing build.c and what it includes.
project
cat >build.c <<'C'
#include "config.h"
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep write = {
        .command = (const char *[]){"sh", "-c", "echo \"$0\" > out", MESSAGE, nullptr},
        .outputs = (const char *[]){"out", nullptr},
    };
    smith_step(b, &write);
    smith_default(b, write.outputs);
}
C
echo '#define MESSAGE "v1"' >config.h
build
expect "build.c: first output" v1 "$(cat out)"
sed 's/"v1"/"v2"/;' config.h >config.tmp && mv config.tmp config.h
build
expect "build.c: an edited header it includes rebuilds the program" v2 "$(cat out)"
sed 's/MESSAGE, nullptr/MESSAGE, "extra", nullptr/' build.c >build.tmp && mv build.tmp build.c
build
expect "build.c: a changed command reruns the step" v2 "$(cat out)"
expect "build.c: the changed command ran" 0 "$rc"

# 6. Options.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    bool loud = smith_option(b, "loud", "print in capitals");
    const char *name = smith_option_value(b, "name", "world", "who to greet");
    SmithStep greet = {
        .command = (const char *[]){"sh", "-c", "echo \"$0 $1\" > \"$2\"", name, loud ? "loud" : "quiet", "$out", nullptr},
        .outputs = (const char *[]){"greeting", nullptr},
    };
    smith_step(b, &greet);
    smith_default(b, greet.outputs);
}
C
build
expect "options: the fallback" "world quiet" "$(cat greeting)"
build -Dname=Ada
expect "options: -Dname=value reaches the command" "Ada quiet" "$(cat greeting)"
build -Dname=Bob -Dloud
expect "options: a changed value and a flag rerun the step" "Bob loud" "$(cat greeting)"
build -Dname=
expect "options: -Dname= is the empty string" " quiet" "$(cat greeting)"
build -Dnope
expect_status "options: an undeclared -D fails" 2
expect_contains "options: the error lists the declared options" "$err" "-Dname=value  who to greet"
expect_contains "options: the error names the unknown one" "$err" "unknown option -Dnope"
build -Dloud=1
expect_status "options: a value for a flag fails" 2
build -Dname
expect_status "options: a bare -D for a valued option fails" 2

# 7. glob, env and capture.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep join = {
        .command = (const char *[]){"sh", "-c", "cat \"$@\" > joined", "x", "$in", nullptr},
        .inputs = smith_glob(b, "src/*.txt"),
        .outputs = (const char *[]){"joined", nullptr},
    };
    smith_step(b, &join);
    SmithStep env = {
        .command = (const char *[]){"sh", "-c", "echo \"$0\" > \"$1\"", smith_env(b, "SMITH_TEST_VALUE", "unset"), "$out", nullptr},
        .outputs = (const char *[]){"env.out", nullptr},
    };
    smith_step(b, &env);
    SmithStep captured = {
        .command = (const char *[]){"sh", "-c", "echo \"$0\" > \"$1\"", smith_capture(b, (const char *[]){"echo", "from capture", nullptr}), "$out", nullptr},
        .outputs = (const char *[]){"capture.out", nullptr},
    };
    smith_step(b, &captured);
    smith_default(b, smith_cat(b, join.outputs, env.outputs, captured.outputs, nullptr));
}
C
mkdir src
echo a >src/a.txt
build
expect "glob: the first file" a "$(cat joined)"
echo b >src/b.txt
build
expect "glob: a new file is picked up with no edit to build.c" "$(printf 'a\nb')" "$(cat joined)"
expect "env: the fallback" unset "$(cat env.out)"
SMITH_TEST_VALUE=set build
expect "env: a changed variable reruns the step" set "$(cat env.out)"
expect "capture: the value reaches a command" "from capture" "$(cat capture.out)"

# 8. restat and always.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep generate = {
        .command = (const char *[]){"sh", "-c", "if [ \"$(cat \"$0\" 2>/dev/null)\" != fixed ]; then echo fixed > \"$0\"; fi; echo x >> generations", "$out", nullptr},
        .inputs = (const char *[]){"trigger", nullptr},
        .outputs = (const char *[]){"generated", nullptr},
        .restat = true,
    };
    smith_step(b, &generate);
    SmithStep copy = {
        .command = (const char *[]){"sh", "-c", "cp \"$0\" \"$1\"; echo x >> copies", "$in", "$out", nullptr},
        .inputs = generate.outputs,
        .outputs = (const char *[]){"copied", nullptr},
    };
    smith_step(b, &copy);
    smith_default(b, copy.outputs);
}
C
echo 1 >trigger
build
echo 2 >trigger
build
expect "restat: the generator reran" 2 "$(lines generations)"
expect "restat: an unchanged output does not rerun what depends on it" 1 "$(lines copies)"

project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep tick = {
        .command = (const char *[]){"sh", "-c", "echo x >> ticks; touch \"$0\"", "$out", nullptr},
        .outputs = (const char *[]){"tick", nullptr},
        .always = true,
    };
    smith_step(b, &tick);
    smith_default(b, tick.outputs);
}
C
build
build
expect "always: reruns the step every build" 2 "$(lines ticks)"

# 9. Errors.
project
build
expect_status "errors: no build.c fails" 2
expect "errors: no build.c message" "smith: no build.c in this folder" "$err"

project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep s = {.command = (const char *[]){"true", nullptr}, .outputs = (const char *[]){"x", nullptr}};
    smith_step(b, &s);
}
C
build
expect_status "errors: no smith_default fails" 2
expect_contains "errors: no smith_default message" "$err" "no default; call smith_default"

# An empty default, such as a glob that matched nothing, builds nothing rather than every target.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep s = {.command = (const char *[]){"touch", "$out", nullptr}, .outputs = (const char *[]){"x", nullptr}};
    smith_step(b, &s);
    smith_default(b, smith_glob(b, "*.none"));
}
C
build
expect "empty default: builds nothing and exits 0" "0 absent" "$rc $([ -e x ] && echo present || echo absent)"
build -f other.ninja
expect_status "errors: -f is refused" 2
expect_contains "errors: -f message" "$err" "smith build names the manifest itself"
build -Cdir
expect_status "errors: -CX is refused" 2

project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep s = {.command = (const char *[]){"false", nullptr}, .outputs = (const char *[]){"x", nullptr}};
    smith_step(b, &s);
    smith_default(b, s.outputs);
}
C
build
expect_status "errors: a failing step makes smith build fail" fail

project
printf 'int build(void) { return ; }\n' >build.c
build
expect_status "errors: a compile error in build.c fails" fail
expect_contains "errors: clang's message is shown" "$out$err" "error:"

project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_defer(b, (const char *[]){nullptr}, nullptr);
}
C
build
expect_status "errors: an unimplemented call exits 2" 2
expect_contains "errors: the unimplemented message" "$err" "smith: build.c: smith_defer is not implemented yet"

# 10. ninja arguments pass through (the copy project of case 1).
cd "$copy_project" || exit 1
build -t targets
expect_contains "ninja arguments: -t targets runs ninja's tool" "$out" "output"
rm output
build -n
expect "ninja arguments: -n does not build" "no" "$([ -e output ] && echo yes || echo no)"

# 11. A smith built with one command.
project
capture "$cc" -std=gnu23 "$here/../smith.c" -o "$work/one-command-smith"
expect "one command: cc -std=gnu23 smith.c builds a smith" 0 "$rc"
capture "$work/one-command-smith" --smith-version
expect_contains "one command: the built smith runs" "$out" "smith 0."

# 12. A no-op build, timed (informational).
cd "$copy_project" || exit 1
build
start=$(perl -MTime::HiRes=time -e 'printf "%.6f", time')
for _ in 1 2 3 4 5 6 7 8 9 10; do "$smith" build >/dev/null 2>&1; done
end=$(perl -MTime::HiRes=time -e 'printf "%.6f", time')
echo "time  no-op smith build: $(perl -e "printf '%.0f', ($end - $start) * 100") ms per run (mean of 10)"

echo
echo "$total build checks, $failures failed"
[ "$failures" -eq 0 ]
