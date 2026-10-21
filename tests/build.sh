#!/bin/sh
# End-to-end tests for `smith build`, with real ninja and the real clang smith picks. Each case runs in its own
# temporary project. Usage: sh tests/build.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")" && pwd)
cc=${CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
export XDG_CACHE_HOME=$work/xdg-cache # the module and package caches of the tests, never the real ones

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
        .inputs = smith_glob(b, (const char *[]){"src/*.txt", nullptr}),
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
    smith_default(b, smith_glob(b, (const char *[]){"*.none", nullptr}));
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
    SmithStep step = {
        .command = (const char *[]){"true", nullptr},
        .outputs = (const char *[]){"x", nullptr},
        .content_hash = true,
    };
    smith_step(b, &step);
}
C
build
expect_status "errors: an unimplemented field exits 2" 2
expect_contains "errors: the unimplemented message" "$err" "smith: build.c: .content_hash is not implemented yet"

# 9b. smith's files are in .smith/, not build/.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep clean = {.command = (const char *[]){"rm", "-rf", "build", nullptr}};
    smith_task(b, "clean", &clean);
    SmithStep write = {
        .command = (const char *[]){"sh", "-c", "echo x > out", nullptr},
        .outputs = (const char *[]){"out", nullptr},
    };
    smith_step(b, &write);
    smith_default(b, write.outputs);
}
C
build
expect "folder: a build whose steps write nothing under build/ leaves no build/" "0 absent" \
    "$rc $([ -e build ] && echo present || echo absent)"
expect "folder: smith keeps its files in .smith" "present present" \
    "$([ -f .smith/build.ninja ] && echo present) $([ -f .smith/include/smith/build.h ] && echo present)"
expect "folder: .smith/.gitignore holds *" "*" "$(cat .smith/.gitignore 2>/dev/null)"
mkdir build
echo x >build/file
build clean
expect "clean: a task running rm -rf build exits 0" "0 absent" "$rc $([ -e build ] && echo present || echo absent)"
rm -f out
build
expect "clean: the next smith build works" "0 present" "$rc $([ -f out ] && echo present || echo absent)"

# 9c. smith_defer and smith_lines, the monad3 shape: a step makes a list, and each name in it becomes a step.
defer_project() {
    project
    printf 'a\nb\n' >source
    cat >build.c <<'C'
#include <smith/build.h>
#include <stddef.h>

static const char *suffix = "";

static void more(SmithBuild *b) {
    const char *const *names = smith_lines(b, "list");
    for (size_t i = 0; names[i] != nullptr; i++) {
        const char *out = smith_fmt(b, "%s.out", names[i]);
        SmithStep step = {
            .command = (const char *[]){"sh", "-c", "echo \"$0$2\" > \"$1\"; echo \"$0\" >> ran", names[i], out, suffix,
                                        nullptr},
            .outputs = (const char *[]){out, nullptr},
        };
        smith_step(b, &step);
        smith_default(b, step.outputs);
    }
}

void build(SmithBuild *b) {
    suffix = smith_option_value(b, "suffix", "", "text added to every output");
    SmithStep list = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = (const char *[]){"source", nullptr},
        .outputs = (const char *[]){"list", nullptr},
    };
    smith_step(b, &list);
    smith_defer(b, list.outputs, more);
}
C
}

defer_project ""
build
expect "defer: a fresh tree builds every output in one smith build" "0 a.out b.out" "$rc $(ls ./*.out | tr '\n' ' ' | sed 's|\./||g; s| *$||')"
build
expect_contains "defer: the second run has no work" "$out" "no work to do"
printf 'a\nb\nc\n' >source
build
expect "defer: a new name builds one new output" "0 a b c" "$rc $(sort ran | tr '\n' ' ' | sed 's| *$||')"
expect "defer: no other output step reran" 3 "$(lines ran)"
build
expect_contains "defer: and the next run has no work" "$out" "no work to do"

defer_project ""
build b.out
expect "defer: a stage 2 target can be named on a fresh tree" "0 b.out" "$rc $(ls ./*.out | sed 's|\./||')"

defer_project ""
build -Dsuffix=x
expect "defer: a -D option reaches stage 2" "bx" "$(cat b.out)"

defer_project ""
build -t clean
build
expect "defer: clean, then smith build, works" "0 a b" "$rc $(sort ran | tr '\n' ' ' | sed 's| *$||')"
build -t clean
expect "defer: clean keeps the manifest" "present" "$([ -f .smith/build.ninja ] && echo present)"
build
expect "defer: and builds again" "0 present present" "$rc $([ -f a.out ] && echo present) $([ -f list ] && echo present)"

# a chained defer: more defers on an output of a step it added
project
printf 'a\nb\n' >source
cat >build.c <<'C'
#include <smith/build.h>
#include <stddef.h>

static void last(SmithBuild *b) {
    const char *const *names = smith_lines(b, "second");
    SmithStep step = {
        .command = (const char *[]){"sh", "-c", "echo \"$0\" > \"$1\"", names[0], "final", nullptr},
        .inputs = (const char *[]){"second", nullptr},
        .outputs = (const char *[]){"final", nullptr},
    };
    smith_step(b, &step);
    smith_default(b, step.outputs);
}

static void middle(SmithBuild *b) {
    SmithStep step = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = (const char *[]){"list", nullptr},
        .outputs = (const char *[]){"second", nullptr},
    };
    smith_step(b, &step);
    smith_defer(b, step.outputs, last);
}

void build(SmithBuild *b) {
    SmithStep list = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = (const char *[]){"source", nullptr},
        .outputs = (const char *[]){"list", nullptr},
    };
    smith_step(b, &list);
    smith_defer(b, list.outputs, middle);
}
C
build
expect "defer: a chained defer builds the whole chain in one smith build" "0 a" "$rc $(cat final 2>/dev/null)"
build
expect_contains "defer: the chain has no work next" "$out" "no work to do"

# smith_lines
project
printf 'one\n\nthree\n' >data
cat >build.c <<'C'
#include <smith/build.h>
#include <stddef.h>

void build(SmithBuild *b) {
    const char *const *lines = smith_lines(b, "data");
    const char *text = smith_fmt(b, "%zu", (size_t)0);
    size_t count = 0;
    while (lines[count] != nullptr)
        count++;
    text = smith_fmt(b, "%zu[%s][%s][%s]", count, lines[0], lines[1], lines[2]);
    SmithStep step = {
        .command = (const char *[]){"sh", "-c", "echo \"$0\" > \"$1\"", text, "result", nullptr},
        .outputs = (const char *[]){"result", nullptr},
    };
    smith_step(b, &step);
    smith_default(b, step.outputs);
}
C
build
expect "lines: three lines, a final newline and an empty one between" "0 3[one][][three]" "$rc $(cat result 2>/dev/null)"
printf 'x' >data
cat >build.c <<'C'
#include <smith/build.h>
#include <stddef.h>

void build(SmithBuild *b) {
    const char *const *lines = smith_lines(b, "data");
    SmithStep step = {
        .command = (const char *[]){"sh", "-c", "echo \"$0\" > \"$1\"", lines[0], "result", nullptr},
        .outputs = (const char *[]){"result", nullptr},
    };
    smith_step(b, &step);
    smith_default(b, step.outputs);
}
C
build
expect "lines: no final newline still gives the line" "0 x" "$rc $(cat result 2>/dev/null)"
: >data
cat >build.c <<'C'
#include <smith/build.h>
#include <stddef.h>

void build(SmithBuild *b) {
    const char *const *lines = smith_lines(b, "data");
    SmithStep step = {
        .command = (const char *[]){"sh", "-c", "echo \"$0\" > \"$1\"", lines[0] == nullptr ? "empty" : "no", "result", nullptr},
        .outputs = (const char *[]){"result", nullptr},
    };
    smith_step(b, &step);
    smith_default(b, step.outputs);
}
C
build
expect "lines: an empty file is an empty list" "0 empty" "$rc $(cat result 2>/dev/null)"
rm data
build
expect_status "lines: a missing file exits 2" 2
expect_contains "lines: the missing file message" "$err" "smith: build.c: cannot read data; if a step makes it, read it inside smith_defer"

# 9d. A command word may hold newlines: the command line goes into .smith/command/<hash> and the edge runs it with sh.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep script = {
        .command = (const char *[]){"sh", "-c",
                                    "if [ \"$1\" = go ]; then\n    echo yes > \"$2\"\nelse\n    echo no > \"$2\"\nfi\necho x >> runs\n",
                                    "sh", "go", "$out", nullptr},
        .inputs = (const char *[]){"input", nullptr},
        .outputs = (const char *[]){"result", nullptr},
    };
    smith_step(b, &script);
    smith_default(b, script.outputs);
}
C
echo a >input
build
expect "newline: a multi-line sh -c script with positional arguments runs" "0 yes 1" "$rc $(cat result 2>/dev/null) $(lines runs)"
expect_contains "newline: the edge runs a file with sh" "$(cat .smith/build.ninja)" "cmd = sh .smith/command/"
expect "newline: the command is one file with a 16 digit name" 1 "$(ls .smith/command | grep -c '^[0-9a-f]\{16\}$')"
command_file=$(ls .smith/command/* | head -n 1)
touch -t 200001010000 "$command_file"
touch marker
build
expect "newline: a second build changes nothing and keeps the file" "smith: no work to do. 0" \
    "$(echo "$out" | tail -n 1) $(find "$command_file" -newer marker | wc -l | tr -d ' ')"
sed 's/echo yes/echo YES/' build.c >build.tmp && mv build.tmp build.c
build
expect "newline: editing the script reruns the step" "0 YES 2" "$rc $(cat result) $(lines runs)"
expect "newline: and writes a new command file" 2 "$(ls .smith/command | grep -c '^[0-9a-f]\{16\}$')"

project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep step = {
        .command = (const char *[]){"true", nullptr},
        .outputs = (const char *[]){"x", nullptr},
        .description = "two\nlines",
    };
    smith_step(b, &step);
    smith_default(b, step.outputs);
}
C
build
expect_status "newline: a newline in a description is still an error" 2
expect_contains "newline: the description message" "$err" "holds a newline"

# 9e. smith_write.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_write(b, "gen/deep/list.txt", smith_option_value(b, "text", "one", "the file's text"));
    SmithStep copy = {
        .command = (const char *[]){"sh", "-c", "cp \"$0\" \"$1\"; echo x >> copies", "$in", "$out", nullptr},
        .inputs = (const char *[]){"gen/deep/list.txt", nullptr},
        .outputs = (const char *[]){"copy.txt", nullptr},
    };
    smith_step(b, &copy);
    smith_default(b, copy.outputs);
}
C
build
expect "write: the bytes, in a folder smith made" "0 one 1" "$rc $(cat gen/deep/list.txt 2>/dev/null) $(lines copies)"
touch -t 200001010000 gen/deep/list.txt
touch marker
build -Dtext=one
expect "write: the same text keeps the mtime and reruns nothing" "0 0 1" \
    "$rc $(find gen/deep/list.txt -newer marker | wc -l | tr -d ' ') $(lines copies)"
build -Dtext=two
expect "write: a new text rewrites it and reruns the step" "0 two 2" "$rc $(cat copy.txt) $(lines copies)"
expect "write: no temporary file is left" 0 "$(ls gen/deep | grep -c tmp)"
build -t clean
expect "write: -t clean leaves it" "present absent" \
    "$([ -f gen/deep/list.txt ] && echo present) $([ -f copy.txt ] && echo present || echo absent)"

project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_write(b, smith_option_value(b, "path", "ok.txt", "where to write"), "x");
    SmithStep step = {.command = (const char *[]){"true", nullptr}, .outputs = (const char *[]){"y", nullptr}};
    smith_step(b, &step);
    smith_default(b, step.outputs);
}
C
for path in /abs.txt ../up.txt a/../b.txt .smith/x.txt ./.smith/x.txt dir/; do
    build "-Dpath=$path"
    expect "write: '$path' is refused" "2 yes" "$rc $(echo "$err" | grep -q '^smith: build.c: smith_write' && echo yes)"
done
build -Dpath=./fine/ok.txt
expect "write: ./folder/file is accepted" "0 x" "$rc $(cat fine/ok.txt 2>/dev/null)"

# 9f. smith_capture_or.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *failed = smith_capture_or(b, (const char *[]){"false", nullptr}, "fb1");
    const char *missing = smith_capture_or(b, (const char *[]){"smith-no-such-program", nullptr}, "fb2");
    const char *worked = smith_capture_or(b, (const char *[]){"echo", "hi", nullptr}, "fb3");
    const char *noisy = smith_capture_or(b, (const char *[]){"sh", "-c", "echo oops >&2; exit 3", nullptr}, "fb4");
    const char *killed = smith_capture_or(b, (const char *[]){"sh", "-c", "kill -9 $$", nullptr}, "fb5");
    const char *none = smith_capture_or(b, (const char *[]){"false", nullptr}, nullptr);
    SmithStep step = {
        .command = (const char *[]){"sh", "-c", "echo \"$0|$1|$2|$3|$4|$5\" > \"$6\"", failed, missing, worked, noisy,
                                    killed, none == nullptr ? "null" : "set", "$out", nullptr},
        .outputs = (const char *[]){"result", nullptr},
    };
    smith_step(b, &step);
    smith_default(b, step.outputs);
}
C
build
expect "capture_or: failed, missing, working, noisy, killed and a nullptr fallback" "0 fb1|fb2|hi|fb4|fb5|null" \
    "$rc $(cat result 2>/dev/null)"
expect "capture_or: stderr goes to /dev/null" "" "$err"

project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *text = smith_capture(b, (const char *[]){"false", nullptr});
    SmithStep step = {.command = (const char *[]){"echo", text, nullptr}};
    smith_task(b, "t", &step);
    smith_default(b, step.outputs);
}
C
build
expect_status "capture: a failing program is still an error" 2

# 9g. A step, a task or an alias declared twice.
project
cat >build.c <<'C'
#include <smith/build.h>

static SmithStep object(SmithBuild *b, const char *description) {
    SmithStep step = {
        .command = (const char *[]){"sh", "-c", "echo x >> runs; echo o > \"$0\"", "$out", nullptr},
        .inputs = (const char *[]){"input", nullptr},
        .outputs = (const char *[]){"shared.o", nullptr},
        .description = description,
    };
    smith_step(b, &step);
    return step;
}

void build(SmithBuild *b) {
    bool differ = smith_option(b, "differ", "declare the second step differently");
    SmithStep first = object(b, "make shared.o");
    SmithStep second = object(b, differ ? "other" : "make shared.o");
    SmithStep tool = {.command = (const char *[]){"sh", "-c", "echo x >> tasks", nullptr}, .inputs = first.outputs};
    smith_task(b, "tool", &tool);
    SmithStep again = {.command = (const char *[]){"sh", "-c", "echo x >> tasks", nullptr}, .inputs = second.outputs};
    smith_task(b, "tool", &again);
    smith_alias(b, "group", tool.outputs);
    smith_alias(b, "group", again.outputs);
    smith_default(b, smith_cat(b, first.outputs, second.outputs, nullptr));
}
C
echo a >input
build
expect "twice: identical steps are accepted and run once" "0 1" "$rc $(lines runs)"
build tool
expect "twice: identical tasks and aliases are one task" "0 1" "$rc $(lines tasks)"
build group
expect "twice: and the alias names it" "0 2" "$rc $(lines tasks)"
build -Ddiffer
expect_status "twice: a step that differs is an error" 2
expect_contains "twice: the error names the output and the field" "$err" \
    "the output 'shared.o' is already made by an earlier step whose .description differs"

project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep first = {.command = (const char *[]){"true", nullptr}};
    smith_task(b, "tool", &first);
    SmithStep second = {.command = (const char *[]){"false", nullptr}};
    smith_task(b, "tool", &second);
    smith_default(b, first.outputs);
}
C
build
expect_status "twice: a task that differs is an error" 2
expect_contains "twice: the task error names the field" "$err" \
    "task tool: the task is already declared and its .command differs"

project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep step = {.command = (const char *[]){"true", nullptr}};
    smith_task(b, "tool", &step);
    smith_alias(b, "group", step.outputs);
    smith_alias(b, "group", (const char *[]){nullptr});
    smith_default(b, step.outputs);
}
C
build
expect_status "twice: an alias with other targets is an error" 2
expect_contains "twice: the alias error" "$err" "alias group: the alias is already declared and its targets differ"

# 9h. Task and alias names may hold '/'.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep step = {.command = (const char *[]){"sh", "-c", "echo ran >> log", nullptr}};
    smith_task(b, smith_option_value(b, "name", "check/src/a.c", "a task name"), &step);
    smith_alias(b, "group/all", step.outputs);
    smith_default(b, (const char *[]){nullptr});
}
C
build check/src/a.c
build group/all
expect "names: a task named after a file runs, and so does an alias with a '/'" "0 2" "$rc $(lines log)"
for name in /x -x x/ a/../b a//b ./a 'a b' 'a:b'; do
    build "-Dname=$name"
    expect "names: '$name' is refused" "2 yes" "$rc $(echo "$err" | grep -q "name '$name'" && echo yes)"
done

# 9i. An option can be read again, in a smith_defer callback.
project
cat >build.c <<'C'
#include <smith/build.h>
#include <stdlib.h>

static void more(SmithBuild *b) {
    const char *name = smith_option_value(b, "name", nullptr, nullptr);
    bool loud = smith_option(b, "loud", nullptr);
    if (getenv("SMITH_TEST_BAD") != nullptr) smith_option(b, "name", nullptr);
    SmithStep step = {
        .command = (const char *[]){"sh", "-c", "echo \"$0 $1\" > \"$2\"", name, loud ? "loud" : "quiet", "$out", nullptr},
        .outputs = (const char *[]){"greeting", nullptr},
    };
    smith_step(b, &step);
    smith_default(b, step.outputs);
}

void build(SmithBuild *b) {
    smith_option_value(b, "name", "world", "who to greet");
    smith_option(b, "loud", "print loudly");
    smith_defer(b, (const char *[]){"source", nullptr}, more);
}
C
echo s >source
build -Dname=Ada -Dloud
expect "option again: a callback reads the values build() read" "0 Ada loud" "$rc $(cat greeting 2>/dev/null)"
build
expect "option again: and the fallback" "0 world quiet" "$rc $(cat greeting 2>/dev/null)"
SMITH_TEST_BAD=1 build
expect_status "option again: the other kind is an error" 2
expect_contains "option again: the message" "$err" "option 'name' is declared as a value and read as a flag"

# 9j. smith_glob takes several patterns: sorted, each name once.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep join = {
        .command = (const char *[]){"sh", "-c", "printf '%s\\n' \"$@\" > joined", "x", "$in", nullptr},
        .inputs = smith_glob(b, (const char *[]){"src/*.txt", "src/a*", "other/*.md", "none*", nullptr}),
        .outputs = (const char *[]){"joined", nullptr},
    };
    smith_step(b, &join);
    smith_default(b, join.outputs);
}
C
mkdir src other
echo a >src/a.txt
echo b >src/b.txt
echo c >other/c.md
build
expect "glob: the union is sorted with each name once" "0 other/c.md src/a.txt src/b.txt" \
    "$rc $(tr '\n' ' ' <joined | sed 's| *$||')"

# 9k. C functions as steps: .run.
project
cat >build.c <<'C'
#include <smith/build.h>
#include <stdio.h>
#include <string.h>

static bool in_list(const char *const *list, const char *string) {
    for (; *list; list++) {
        if (strcmp(*list, string) == 0) return true;
    }
    return false;
}

static int check_libc(SmithRun *run) {
    const char *const *ours = (const char *[]){nullptr};
    for (const char *const *object = run->inputs; *object; object++) {
        const char *const *defined = smith_run_lines(run, (const char *[]){"nm", "-gUj", *object, nullptr});
        if (defined == nullptr) return 1;
        ours = smith_cat(run->b, ours, defined, nullptr);
    }
    int failed = 0;
    for (const char *const *object = run->inputs; *object; object++) {
        const char *const *used = smith_run_lines(run, (const char *[]){"nm", "-uj", *object, nullptr});
        if (used == nullptr) return 1;
        for (const char *const *symbol = used; *symbol; symbol++) {
            const char *name = **symbol == '_' ? *symbol + 1 : *symbol;
            if (in_list(ours, *symbol) || in_list(run->args, name)) continue;
            fprintf(stderr, "%s uses %s from the C library, which is not allowed\n", *object, name);
            failed = 1;
        }
    }
    return failed;
}

void build(SmithBuild *b) {
    const char *const sources[] = {"base", "game", nullptr};
    const char *const *objects = (const char *[]){nullptr};
    for (const char *const *name = sources; *name; name++) {
        SmithStep cc = {
            .command = (const char *[]){"cc", "-O0", "-c", "$in", "-o", "$out", nullptr},
            .inputs = (const char *[]){smith_fmt(b, "%s.c", *name), nullptr},
            .outputs = (const char *[]){smith_fmt(b, "build/%s.o", *name), nullptr},
        };
        smith_step(b, &cc);
        objects = smith_cat(b, objects, cc.outputs, nullptr);
    }
    SmithStep check = {
        .run = check_libc,
        .args = (const char *[]){"memcpy", "memmove", "memset", "__memcpy_chk", "bzero", "sqrtf", "fabsf",
                                 "__stack_chk_guard", "__stack_chk_fail", nullptr},
        .inputs = objects,
        .description = "check-libc",
    };
    smith_step(b, &check);
    smith_alias(b, "check-libc", check.outputs);
    smith_default(b, check.outputs);
}
C
cat >base.c <<'C'
#include <math.h>
#include <string.h>
float base_root(float x) {
    char a[4] = "abc", c[4];
    memcpy(c, a, 4);
    return sqrtf(x) + (float)c[0];
}
C
cat >game.c <<'C'
float base_root(float x);
float game_run(float x) { return base_root(x); }
C
build check-libc
expect "function: a clean tree passes" "0 yes" "$rc $(ls .smith/stamp/* >/dev/null 2>&1 && echo yes || echo no)"
build check-libc
expect_contains "function: a second build does nothing" "$out" "no work to do"
cat >game.c <<'C'
#include <stdio.h>
float base_root(float x);
float game_run(float x) { puts("hi"); return base_root(x); }
C
rm -rf .smith/stamp
build check-libc
expect_status "function: a forbidden symbol fails the build" fail
expect_contains "function: the message names the object and the symbol" "$err$out" \
    "build/game.o uses puts from the C library, which is not allowed"
expect "function: a failed check leaves no stamp" "no" "$(ls .smith/stamp/* >/dev/null 2>&1 && echo yes || echo no)"
cat >game.c <<'C'
float base_root(float x);
float game_run(float x) { return base_root(x); }
C
build check-libc
expect "function: restored, it passes" "0 yes" "$rc $(ls .smith/stamp/* >/dev/null 2>&1 && echo yes || echo no)"

# 9l. A .run step that writes its output with smith_write and .restat; a task with .args; failure; editing build.c.
project
cat >build.c <<'C'
#include <smith/build.h>
#include <stdio.h>
#include <string.h>

static int first_word(SmithRun *run) {
    const char *const *lines = smith_lines(run->b, run->inputs[0]);
    smith_write(run->b, run->outputs[0], smith_fmt(run->b, "%s\n", lines[0]));
    return 0;
}

static int show(SmithRun *run) {
    FILE *log = fopen("runs.log", "a");
    for (const char *const *arg = run->args; *arg; arg++)
        fprintf(log, "%s ", *arg);
    fputc('\n', log);
    fclose(log);
    printf("task saw %s\n", run->args[0]);
    return 0;
}

static int fail_three(SmithRun *run) {
    (void)run;
    fputs("this step fails\n", stderr);
    return 3;
}

void build(SmithBuild *b) {
    SmithStep word = {
        .run = first_word,
        .inputs = (const char *[]){"seed.txt", nullptr},
        .outputs = (const char *[]){"word.txt", nullptr},
        .restat = true,
    };
    smith_step(b, &word);
    SmithStep copy = {
        .command = (const char *[]){"sh", "-c", "cp \"$0\" final.txt && echo x >> copies.log", "$in", nullptr},
        .inputs = word.outputs,
        .outputs = (const char *[]){"final.txt", nullptr},
    };
    smith_step(b, &copy);
    SmithStep task = {.run = show, .args = (const char *[]){"alpha", "beta", nullptr}};
    smith_task(b, "show", &task);
    SmithStep bad = {.run = fail_three, .outputs = (const char *[]){"bad.out", nullptr}};
    smith_step(b, &bad);
    SmithStep after = {
        .command = (const char *[]){"sh", "-c", "echo ran > after.out", nullptr},
        .inputs = bad.outputs,
        .outputs = (const char *[]){"after.out", nullptr},
    };
    smith_step(b, &after);
    smith_alias(b, "broken", after.outputs);
    smith_default(b, copy.outputs);
}
C
printf 'hello world\nline two\n' >seed.txt
build
expect "restat: the .run step writes its output" "0 hello world" "$rc $(cat final.txt 2>/dev/null)"
printf 'hello world\nline two changed\n' >seed.txt
build
expect "restat: the .run step reran and the bytes are the same" "1" "$(lines copies.log)"
printf 'goodbye world\nline two\n' >seed.txt
build
expect "restat: changed bytes rerun what depends on it" "goodbye world 2" "$(cat final.txt) $(lines copies.log)"
build show
expect_contains "task: runs and prints" "$out" "task saw alpha"
build show
expect "task: runs every time, with .args" "2 alpha beta" "$(lines runs.log) $(sed -n 1p runs.log | sed 's/ *$//')"
build broken
expect_status "failure: a function returning 3 fails the build" fail
expect_contains "failure: ninja reports the status" "$out$err" "code=3"
expect_contains "failure: the function's own message shows" "$out$err" "this step fails"
expect "failure: nothing after it ran" "no" "$([ -e after.out ] && echo yes || echo no)"

# 9m. Editing the function reruns the step; options reach the function through .args; defer.
project
cat >build.c <<'C'
#include <smith/build.h>

static int write_it(SmithRun *run) {
    smith_write(run->b, run->outputs[0], smith_fmt(run->b, "v1 %s\n", run->args[0]));
    return 0;
}

static int deferred(SmithRun *run) {
    smith_write(run->b, run->outputs[0], "from defer\n");
    return 0;
}

static void more(SmithBuild *b) {
    SmithStep step = {.run = deferred, .outputs = (const char *[]){"deferred.out", nullptr}};
    smith_step(b, &step);
    smith_default(b, step.outputs);
}

void build(SmithBuild *b) {
    const char *who = smith_option_value(b, "who", "nobody", "a name");
    SmithStep step = {
        .run = write_it,
        .args = (const char *[]){who, nullptr},
        .outputs = (const char *[]){"it.out", nullptr},
    };
    smith_step(b, &step);
    smith_alias(b, "it", step.outputs);
    smith_defer(b, (const char *[]){"it.out", nullptr}, more);
}
C
build it
expect "edit: the function runs" "0 v1 nobody" "$rc $(cat it.out 2>/dev/null)"
build it
expect_contains "edit: unchanged, nothing reruns" "$out" "no work to do"
sed 's/"v1 %s/"v2 %s/' build.c >build.c.new && mv build.c.new build.c
build it
expect "edit: editing build.c reruns the step" "0 v2 nobody" "$rc $(cat it.out 2>/dev/null)"
build it -Dwho=Ada
expect "options: -Dname=value reaches the function through .args" "0 v2 Ada" "$rc $(cat it.out 2>/dev/null)"
build it -Dwho='a b$c'
expect "options: a value with a space and a dollar" "v2 a b\$c" "$(cat it.out 2>/dev/null)"
build
expect "defer: a .run step added in a smith_defer callback runs" "0 from defer" "$rc $(cat deferred.out 2>/dev/null)"

# 9n. smith_run_status and smith_run_lines.
project
cat >build.c <<'C'
#include <smith/build.h>
#include <stdio.h>

static int probe(SmithRun *run) {
    const char *const *lines = nullptr;
    FILE *out = fopen("status.txt", "w");
    fprintf(out, "true %d\n", smith_run_status(run, (const char *[]){"true", nullptr}, nullptr));
    fprintf(out, "false %d\n", smith_run_status(run, (const char *[]){"false", nullptr}, &lines));
    fprintf(out, "missing %d lines %s\n", smith_run_status(run, (const char *[]){"smith-no-such-program", nullptr}, &lines),
            lines == nullptr ? "null" : "set");
    int status = smith_run_status(run, (const char *[]){"sh", "-c", "printf 'a\n\nb c\nlast'; exit 4", nullptr}, &lines);
    fprintf(out, "sh %d", status);
    for (const char *const *line = lines; *line; line++)
        fprintf(out, " [%s]", *line);
    fputc('\n', out);
    const char *const *same = smith_run_lines(run, (const char *[]){"printf", "x\ny\n", nullptr});
    fprintf(out, "lines %s %s %s\n", same[0], same[1], same[2] == nullptr ? "end" : "more");
    fprintf(out, "failing %s\n", smith_run_lines(run, (const char *[]){"sh", "-c", "echo oops >&2; exit 5", nullptr}) == nullptr ? "null" : "set");
    fclose(out);
    return 0;
}

void build(SmithBuild *b) {
    SmithStep step = {.run = probe, .outputs = (const char *[]){"status.txt", nullptr}};
    smith_step(b, &step);
    smith_default(b, step.outputs);
}
C
build
expect_status "run_status: the probe step succeeds" 0
expect_contains "run_status: failing program prints the message" "$out$err" "smith: sh failed (status 5)"
expect_contains "run_status: its stderr passes through" "$out$err" "oops"
expect "run_status: statuses and lines are exact" "true 0
false 1
missing 127 lines null
sh 4 [a] [] [b c] [last]
lines x y end
failing null" "$(cat status.txt)"

# 9o. A .run step's cost (informational): a trivial .run task against a trivial command task.
project
cat >build.c <<'C'
#include <smith/build.h>

static int trivial(SmithRun *run) {
    (void)run;
    return 0;
}

void build(SmithBuild *b) {
    SmithStep tick = {.run = trivial};
    smith_task(b, "tick", &tick);
    SmithStep tock = {.command = (const char *[]){"true", nullptr}};
    smith_task(b, "tock", &tock);
    smith_default(b, (const char *[]){nullptr});
}
C
build tick
for task in tock tick; do
    start=$(perl -MTime::HiRes=time -e 'printf "%.6f", time')
    for _ in 1 2 3 4 5 6 7 8 9 10; do "$smith" build $task >/dev/null 2>&1; done
    end=$(perl -MTime::HiRes=time -e 'printf "%.6f", time')
    echo "time  smith build $task (a task that runs every time): $(perl -e "printf '%.0f', ($end - $start) * 100") ms per run (mean of 10)"
done

# 9o2. A .run step gets its data from its command: .args and .inputs with a space and a $ in a value, no -D option, and
# build() is not called again for it.
project
cat >build.c <<'C'
#include <smith/build.h>
#include <stdio.h>

static int show(SmithRun *run) {
    FILE *out = fopen(run->outputs[0], "w");
    for (const char *const *input = run->inputs; *input; input++)
        fprintf(out, "input [%s]\n", *input);
    for (const char *const *output = run->outputs; *output; output++)
        fprintf(out, "output [%s]\n", *output);
    for (const char *const *arg = run->args; *arg; arg++)
        fprintf(out, "arg [%s]\n", *arg);
    fprintf(out, "who [%s]\n", smith_option_value(run->b, "who", "unset", nullptr));
    fclose(out);
    return 0;
}

void build(SmithBuild *b) {
    FILE *count = fopen("calls.log", "a");
    fputs("build\n", count);
    fclose(count);
    smith_option_value(b, "who", "nobody", "a name");
    SmithStep step = {
        .run = show,
        .inputs = (const char *[]){"in put.txt", "$HOME.txt", nullptr},
        .outputs = (const char *[]){"seen.txt", nullptr},
        .args = (const char *[]){"two words", "$dollar", "it's", "--arg", nullptr},
    };
    smith_step(b, &step);
    SmithStep other = {.run = show, .inputs = (const char *[]){"in put.txt", nullptr}, .outputs = (const char *[]){"other.out", nullptr}};
    smith_step(b, &other);
    smith_default(b, smith_cat(b, step.outputs, other.outputs, nullptr));
}
C
echo a >'in put.txt'
echo b >'$HOME.txt'
build -Dwho=Ada
expect_status ".run data: the step succeeds" 0
cat >want.txt <<'W'
input [in put.txt]
input [$HOME.txt]
output [seen.txt]
arg [two words]
arg [$dollar]
arg [it's]
arg [--arg]
who [unset]
W
expect ".run data: inputs, outputs and args arrive exactly, and no -D option does" "$(cat want.txt)" "$(cat seen.txt)"
expect ".run data: build() runs once per smith build, not once per .run step" 1 "$(lines calls.log)"
build -Dwho=Bob
expect ".run data: build() ran once more for a second smith build" 2 "$(lines calls.log)"

# 9o3. smith_join and smith_list.
project
cat >build.c <<'C'
#include <smith/build.h>

static const char *const *make_list(SmithBuild *b) {
    {
        return smith_list(b, "one", "two words", "$three", nullptr);
    }
}

static const char *const *kept(SmithBuild *b) {
    const char *const *list;
    {
        list = smith_list(b, "kept", "alive", nullptr);
        const char *scratch[] = {"gone", nullptr};
        (void)scratch;
    }
    return list;
}

void build(SmithBuild *b) {
    const char *const *empty = smith_list(b, nullptr);
    const char *const *one = smith_list(b, "solo", nullptr);
    const char *const *many = make_list(b);
    smith_write(b, "join.txt",
                smith_fmt(b, "[%s][%s][%s][%s][%s]\n[%s]\n", smith_join(b, empty, ","), smith_join(b, one, ", "),
                          smith_join(b, many, ", "), smith_join(b, many, ""), smith_join(b, kept(b), "\n"),
                          smith_join(b, smith_map(b, many, "#include \"%s\"\n"), "")));
    smith_write(b, "list.txt", smith_fmt(b, "%d %d %d %s\n", (int)(empty[0] == nullptr), (int)(one[1] == nullptr),
                                         (int)(many[3] == nullptr), many[2]));
    smith_default(b, smith_list(b, nullptr));
}
C
build
expect_status "join, list: build.c runs" 0
expect "join: empty, one, several, empty separator, newline, mapped" '[][solo][one, two words, $three][onetwo words$three][kept
alive]
[#include "one"
#include "two words"
#include "$three"
]' "$(cat join.txt)"
expect "list: empty, one, three; kept past its block and returned" '1 1 1 $three' "$(cat list.txt)"

# 9o4. smith_run_program: stdout reaches the terminal, an environment variable arrives, and an exit status comes back.
project
cat >build.c <<'C'
#include <smith/build.h>
#include <stdio.h>

static int play(SmithRun *run) {
    printf("function line first\n");
    int shown = smith_run_program(run, (const char *[]){"sh", "-c", "echo \"program says $SMITH_TEST_WORD and $HOME_KEPT\"; echo err-line >&2", nullptr},
                                  (const char *[]){"SMITH_TEST_WORD=two words $x", "HOME_KEPT=kept", nullptr});
    int three = smith_run_program(run, (const char *[]){"sh", "-c", "exit 3", nullptr}, nullptr);
    int killed = smith_run_program(run, (const char *[]){"sh", "-c", "kill -9 $$", nullptr}, nullptr);
    int missing = smith_run_program(run, (const char *[]){"smith-no-such-program", nullptr}, nullptr);
    printf("statuses %d %d %d %d\n", shown, three, killed, missing);
    return 0;
}

void build(SmithBuild *b) {
    SmithStep step = {.run = play};
    smith_task(b, "play", &step);
    smith_default(b, smith_list(b, nullptr));
}
C
capture env HOME_KEPT=old "$smith" build play
expect_status "run_program: the step succeeds" 0
expect_contains "run_program: the environment variable arrives" "$out" "program says two words \$x and kept"
expect_contains "run_program: statuses are 0, 3, 128 plus the signal and 127" "$out" "statuses 0 3 137 127"
expect "run_program: the function's line comes before the program's" "function line first" \
    "$(printf '%s\n' "$out" | grep -n 'function line first\|program says' | sed -n 1p | sed 's/^[0-9]*://')"
expect_contains "run_program: stderr reaches the terminal" "$out$err" "err-line"
expect "run_program: the program's line is in smith build's own stdout" 1 "$(printf '%s\n' "$out" | grep -c 'program says')"

# 9o5. ** in smith_glob.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_write(b, "deep.txt", smith_join(b, smith_map(b, smith_glob(b, (const char *[]){"src/**/*.c", nullptr}), "%s\n"), ""));
    smith_write(b, "hidden.txt", smith_join(b, smith_map(b, smith_glob(b, (const char *[]){"src/**/*.h", nullptr}), "%s\n"), ""));
    smith_write(b, "overlap.txt", smith_join(b, smith_map(b, smith_glob(b, (const char *[]){"src/**/*.c", "src/x/*.c", "src/*.c", "**/b.c", nullptr}), "%s\n"), ""));
    smith_write(b, "dot.txt", smith_join(b, smith_map(b, smith_glob(b, (const char *[]){"src/.*/*.c", "src/**/.hid/*.h", nullptr}), "%s\n"), ""));
    smith_write(b, "star.txt", smith_join(b, smith_map(b, smith_glob(b, (const char *[]){"*/a.c", "s?c/x/[y]/*.c", nullptr}), "%s\n"), ""));
    smith_default(b, smith_list(b, nullptr));
}
C
mkdir -p src/x/y/z src/.hid/deep other
echo >src/a.c
echo >src/x/b.c
echo >src/x/y/z/d.c
echo >src/x/y/c.c
echo >src/.hid/h.c
echo >src/.hid/h.h
echo >src/x/y/e.h
echo >other/a.c
build
expect_status "glob **: build.c runs" 0
expect "glob **: zero, one, two and three folders deep; the hidden folder is skipped" "src/a.c
src/x/b.c
src/x/y/c.c
src/x/y/z/d.c" "$(cat deep.txt)"
expect "glob **: *.h skips the hidden folder" "src/x/y/e.h" "$(cat hidden.txt)"
expect "glob **: sorted, with no duplicates when patterns overlap" "src/a.c
src/x/b.c
src/x/y/c.c
src/x/y/z/d.c" "$(cat overlap.txt)"
expect "glob **: a component that starts with . matches hidden entries" "src/.hid/h.c
src/.hid/h.h" "$(cat dot.txt)"
expect "glob: other components go through fnmatch" "other/a.c
src/a.c
src/x/y/c.c" "$(cat star.txt)"

# 9p. An option that only a smith_defer callback reads: stage 1 cannot know it yet, stage 2 checks it.
project
cat >build.c <<'C'
#include <smith/build.h>

static void more(SmithBuild *b) {
    const char *suffix = smith_option_value(b, "suffix", "none", "the word written to out");
    SmithStep out = {
        .command = (const char *[]){"sh", "-c", "echo \"$0\" > \"$1\"", suffix, "$out", nullptr},
        .inputs = (const char *[]){"list", nullptr},
        .outputs = (const char *[]){"out", nullptr},
    };
    smith_step(b, &out);
    smith_default(b, out.outputs);
}

void build(SmithBuild *b) {
    SmithStep list = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = (const char *[]){"source", nullptr},
        .outputs = (const char *[]){"list", nullptr},
    };
    smith_step(b, &list);
    smith_defer(b, list.outputs, more);
}
C
echo x >source
build -Dsuffix=late
expect "defer option: read only in the callback, on a fresh tree" "0 late" "$rc $(cat out 2>/dev/null)"
build -Dsufix=typo
expect_status "defer option: a misspelt one still fails once the callback ran" 2
expect_contains "defer option: the message names it" "$err" "unknown option -Dsufix"

# 9h. The high level: smith_object, smith_program and smith_compile_commands.
project
mkdir "src dir"
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithObject main_object = {
        .compiler = "cc",
        .source = "src dir/main.c",
        .output = "build/main.o",
        .flags = (const char *[]){"-std=gnu23", "-DGREETING=\"hi\"", nullptr},
    };
    smith_object(b, &main_object);
    SmithObject util = {.compiler = "cc", .source = "util.c", .output = "build/util.o"};
    smith_object(b, &util);
    SmithProgram app = {
        .linker = "cc",
        .output = "build/app",
        .objects = (const SmithObject *[]){&main_object, &util, nullptr},
        .libraries = (const char *[]){"-lm", "-framework", "Foundation", "libextra.a", nullptr},
    };
    smith_program(b, &app);
    smith_compile_commands(b, "compile_commands.json");
    smith_default(b, app.step.outputs);
}
C
cat >"src dir/main.c" <<'C'
#include <stdio.h>
int util_value(void);
int main(void) { printf("%s %d\n", GREETING, util_value()); return 0; }
C
printf 'int util_value(void);\n' >util.h
printf '#include "util.h"\nint util_value(void) { return 7; }\n' >util.c
printf 'int extra_value(void) { return 1; }\n' >extra.c
"$cc" -c extra.c -o extra.o && ar rcs libextra.a extra.o
build -v
expect "high level: the program builds" 0 "$rc"
expect "high level: the program runs" "hi 7" "$(./build/app 2>/dev/null)"
expect_contains "high level: -v shows the compile" "$out" "cc -std=gnu23 '-DGREETING=\"hi\"' -MMD -MF build/main.d -c 'src dir/main.c' -o build/main.o"
expect_contains "high level: -v shows the link" "$out" "cc build/main.o build/util.o -lm -framework Foundation libextra.a -o build/app"
here_dir=$(pwd -P)
cat >expected.json <<JSON
[
  {
    "directory": "$here_dir",
    "file": "src dir/main.c",
    "output": "build/main.o",
    "arguments": [
      "cc",
      "-std=gnu23",
      "-DGREETING=\\"hi\\"",
      "-MMD",
      "-MF",
      "build/main.d",
      "-c",
      "src dir/main.c",
      "-o",
      "build/main.o"
    ]
  },
  {
    "directory": "$here_dir",
    "file": "util.c",
    "output": "build/util.o",
    "arguments": [
      "cc",
      "-MMD",
      "-MF",
      "build/util.d",
      "-c",
      "util.c",
      "-o",
      "build/util.o"
    ]
  },
  {
    "directory": "$here_dir",
    "file": "build.c",
    "arguments": [
      "smith-cc",
      "-std=gnu23",
      "-g",
      "-O0",
      "-Wall",
      "-Wextra",
      "-I",
      ".smith/include",
      "-c",
      "build.c"
    ]
  }
]
JSON
expect "compile_commands.json: the whole file" "$(cat expected.json)" "$(cat compile_commands.json 2>/dev/null)"
expect "compile_commands.json: ends with a newline" "]" "$(tail -c 2 compile_commands.json | tr -d '\n' | tail -c 1)"
before=$(perl -e 'print((stat("compile_commands.json"))[9])')
sleep 1
build
expect_contains "high level: a no-op build does nothing" "$out" "no work to do"
expect "compile_commands.json: the mtime is unchanged on a no-op" "$before" "$(perl -e 'print((stat("compile_commands.json"))[9])')"
sleep 1
printf 'int util_value(void);\n// edited\n' >util.h
build -v
expect_contains "high level: a header edit rebuilds the includer" "$out" "build/util.o"
expect_contains "high level: a header edit relinks" "$out" "-o build/app"
case $out in *"src dir/main.c"*) rebuilt_main=yes ;; *) rebuilt_main=no ;; esac
expect "high level: a header only util.c includes does not rebuild main" no "$rebuilt_main"
expect "high level: a header edit leaves compile_commands.json alone" "$before" "$(perl -e 'print((stat("compile_commands.json"))[9])')"
sleep 1
touch libextra.a
build -v
expect_contains "high level: a changed library file relinks" "$out" "-o build/app"
build
sleep 1
touch Foundation
build
expect_contains "high level: -framework X's name is not a file" "$out" "no work to do"
sed -i.bak 's/"-std=gnu23", "-DGREETING/"-std=gnu23", "-Wall", "-DGREETING/' build.c
build
case $(cat compile_commands.json) in *'"-Wall"'*) changed=yes ;; *) changed=no ;; esac
expect "compile_commands.json: a changed flag changes it" yes "$changed"
# the build.c entry compiles build.c with exactly these arguments
args=$(sed -n '/"file": "build.c"/,/^    \]/p' compile_commands.json | sed -n 's/^      "\(.*\)",\{0,1\}$/\1/p' | tail -n +2 | grep -v '^-c$' | grep -v '^build.c$')
# shellcheck disable=SC2086
capture "$smith" cc $args -fsyntax-only build.c
expect "build.c entry: clang -fsyntax-only with its arguments succeeds" "0 " "$rc $err"
bootstrap_flags=$(sed -n 's/.*command = [^ ]* \(cc \)\{0,1\}\(-std=gnu23.*-I \.smith\/include\) -MMD.*/\2/p' .smith/bootstrap/build.ninja | head -n 1)
entry_flags=$(echo $args)
expect "build.c entry: its flags are the bootstrap's" "$bootstrap_flags" "$entry_flags"
cd "$copy_project" || exit 1

# 9i. High-level errors.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithObject o = {.source = "a.c", .output = "a.o"};
    smith_object(b, &o);
    smith_default(b, o.step.outputs);
}
C
build
expect_status "errors: a missing .compiler fails" 2
expect_contains "errors: the message names .compiler" "$err" ".compiler is required"
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithObject o = {.compiler = "cc", .source = "a.c", .output = "a.o"};
    smith_object(b, &o);
    SmithObject copy = o;
    SmithProgram p = {.linker = "cc", .output = "a", .objects = (const SmithObject *[]){&copy, nullptr}};
    smith_program(b, &p);
    smith_default(b, p.step.outputs);
}
C
build
expect_status "errors: an unregistered object fails" 2
expect_contains "errors: the message says it was never passed to smith_object" "$err" "never passed to smith_object"
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_compile_commands(b, "one.json");
    smith_compile_commands(b, "two.json");
    smith_default(b, (const char *[]){nullptr});
}
C
build
expect_status "errors: a second smith_compile_commands fails" 2
expect_contains "errors: the message says twice" "$err" "twice"
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_compile_commands(b, ".smith/cc.json");
    smith_default(b, (const char *[]){nullptr});
}
C
build
expect_status "errors: compile_commands under .smith fails" 2

# 9j. smith_compile_command: an entry with no step.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithObject util = {.compiler = "cc", .source = "util.c", .output = "build/util.o"};
    smith_object(b, &util);
    smith_compile_command(b, "cc", "tests/a.c", (const char *[]){"-std=gnu23", "-Iinc", nullptr});
    smith_compile_command(b, "cc", "util.c", (const char *[]){"-DIGNORED", nullptr});
    smith_compile_command(b, "cc", "tests/b.c", nullptr);
    smith_compile_command(b, "cc", "tests/a.c", (const char *[]){"-DIGNORED", nullptr});
    smith_compile_commands(b, "compile_commands.json");
    smith_default(b, util.step.outputs);
}
C
printf 'int util_value(void) { return 7; }\n' >util.c
build
expect "compile_command: the build succeeds" 0 "$rc"
here_dir=$(pwd -P)
cat >expected.json <<JSON
[
  {
    "directory": "$here_dir",
    "file": "util.c",
    "output": "build/util.o",
    "arguments": [
      "cc",
      "-MMD",
      "-MF",
      "build/util.d",
      "-c",
      "util.c",
      "-o",
      "build/util.o"
    ]
  },
  {
    "directory": "$here_dir",
    "file": "tests/a.c",
    "arguments": [
      "cc",
      "-std=gnu23",
      "-Iinc",
      "-c",
      "tests/a.c"
    ]
  },
  {
    "directory": "$here_dir",
    "file": "tests/b.c",
    "arguments": [
      "cc",
      "-c",
      "tests/b.c"
    ]
  },
  {
    "directory": "$here_dir",
    "file": "build.c",
    "arguments": [
      "smith-cc",
      "-std=gnu23",
      "-g",
      "-O0",
      "-Wall",
      "-Wextra",
      "-I",
      ".smith/include",
      "-c",
      "build.c"
    ]
  }
]
JSON
expect "compile_command: the entries, their order, and a covered file skipped" "$(cat expected.json)" "$(cat compile_commands.json 2>/dev/null)"
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_compile_command(b, nullptr, "a.c", nullptr);
    smith_default(b, (const char *[]){nullptr});
}
C
build
expect_status "errors: a missing compiler for smith_compile_command fails" 2
expect_contains "errors: the message names compiler" "$err" "smith_compile_command: .compiler is required"
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_compile_command(b, "cc", "", nullptr);
    smith_default(b, (const char *[]){nullptr});
}
C
build
expect_status "errors: a missing source for smith_compile_command fails" 2
expect_contains "errors: the message names source" "$err" "smith_compile_command: .source is required"
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_compile_commands(b, "compile_commands.json");
    smith_compile_command(b, "cc", "a.c", nullptr);
    smith_default(b, (const char *[]){nullptr});
}
C
build
expect_status "errors: smith_compile_command after smith_compile_commands fails" 2
expect_contains "errors: the message says after" "$err" "after smith_compile_commands"

# 9p. .stamp: a syntax check whose result is that it passed, with a depfile that names the stamp.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *warn = smith_option_value(b, "W", "-Wall", "a warning flag");
    SmithStep check = {
        .command = (const char *[]){"cc", "-std=gnu23", warn, "-Werror", "-fsyntax-only", "-MMD", "-MF",
                                    "build/check/a.d", "-MT", "build/check/a.stamp", "a.c", nullptr},
        .inputs = (const char *[]){"a.c", nullptr},
        .stamp = "build/check/a.stamp",
        .depfile = "build/check/a.d",
        .description = "check a",
    };
    smith_step(b, &check);
    SmithStep same = {
        .command = (const char *[]){"cc", "-std=gnu23", warn, "-Werror", "-fsyntax-only", "-MMD", "-MF",
                                    "build/check/a.d", "-MT", "build/check/a.stamp", "a.c", nullptr},
        .inputs = (const char *[]){"a.c", nullptr},
        .stamp = "build/check/a.stamp",
        .depfile = "build/check/a.d",
        .description = "check a",
    };
    smith_step(b, &same);
    smith_alias(b, "check", check.outputs);
    smith_default(b, check.outputs);
}
C
printf '#include "h.h"\nint a(void) { return H; }\n' >a.c
printf '#define H 1\n' >h.h
build
expect "stamp: the check passes and leaves its stamp" "0 yes" "$rc $([ -f build/check/a.stamp ] && echo yes || echo no)"
expect_contains "stamp: it ran" "$out" "check a"
build
expect_contains "stamp: unchanged, nothing reruns" "$out" "no work to do"
printf '\n' >>a.c
build
expect_contains "stamp: a changed source reruns" "$out" "check a"
build
expect_contains "stamp: then it settles" "$out" "no work to do"
printf '#define H 2\n' >h.h
build
expect_contains "stamp: a changed header reruns" "$out" "check a"
build
expect_contains "stamp: then it settles again" "$out" "no work to do"
build -DW=-Wextra
expect_contains "stamp: changed flags rerun" "$out" "check a"
build -DW=-Wextra
expect_contains "stamp: the same flags settle" "$out" "no work to do"
printf 'int a(void) { return ; }\n' >a.c
rm -f build/check/a.stamp
build -DW=-Wextra
expect_status "stamp: a failing check fails the build" fail
expect "stamp: its failure leaves no stamp" "no" "$([ -f build/check/a.stamp ] && echo yes || echo no)"
build -DW=-Wextra
expect_status "stamp: it fails again, so it reran" fail
printf 'int a(void) { return 0; }\n' >a.c
build -DW=-Wextra
expect "stamp: fixed, it passes and stamps" "0 yes" "$rc $([ -f build/check/a.stamp ] && echo yes || echo no)"

# 9q. .stamp errors: with .outputs, on a task, a different .stamp for an existing output.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep step = {.command = (const char *[]){"true", nullptr}, .stamp = "s", .outputs = (const char *[]){"o", nullptr}};
    smith_step(b, &step);
    smith_default(b, step.outputs);
}
C
build
expect_status "stamp errors: .stamp with .outputs fails" 2
expect_contains "stamp errors: the message names both" "$err" ".stamp"
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep task = {.command = (const char *[]){"true", nullptr}, .stamp = "s"};
    smith_task(b, "t", &task);
    smith_default(b, task.outputs);
}
C
build
expect_status "stamp errors: .stamp on a task fails" 2
expect_contains "stamp errors: the message says task" "$err" "task t"
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep one = {.command = (const char *[]){"touch", "s", nullptr}, .outputs = (const char *[]){"s", nullptr}};
    smith_step(b, &one);
    SmithStep two = {.command = (const char *[]){"touch", "s", nullptr}, .stamp = "s"};
    smith_step(b, &two);
    smith_default(b, one.outputs);
}
C
build
expect_status "stamp errors: a different .stamp for an output fails" 2
expect_contains "stamp errors: the message names the field" "$err" "whose .stamp differs"
cat >build.c <<'C'
#include <smith/build.h>

static int pass(SmithRun *run) {
    (void)run;
    return 0;
}

void build(SmithBuild *b) {
    SmithStep step = {.run = pass, .stamp = "build/pass.stamp"};
    smith_step(b, &step);
    SmithStep command = {.command = (const char *[]){"true", nullptr}, .stamp = "build/true.stamp"};
    smith_step(b, &command);
    smith_default(b, smith_cat(b, step.outputs, command.outputs, nullptr));
}
C
build
expect "stamp: a .run step and a command step both stamp" "0 yes yes" "$rc $([ -f build/pass.stamp ] && echo yes || echo no) $([ -f build/true.stamp ] && echo yes || echo no)"

# 9r. smith_run_combined: stdout and stderr in one stream, in order; statuses as smith_run_status gives.
project
cat >build.c <<'C'
#include <smith/build.h>
#include <stdio.h>

static int probe(SmithRun *run) {
    const char *const *lines = nullptr;
    FILE *out = fopen("combined.txt", "w");
    int status = smith_run_combined(
        run, (const char *[]){"sh", "-c", "echo o1; echo e1 >&2; echo o2; echo e2 >&2; echo o3; exit 4", nullptr},
        &lines);
    fprintf(out, "sh %d", status);
    for (const char *const *line = lines; *line; line++)
        fprintf(out, " [%s]", *line);
    fputc('\n', out);
    status = smith_run_combined(run, (const char *[]){"sh", "-c", "echo before; kill -9 $$", nullptr}, &lines);
    fprintf(out, "killed %d %s\n", status, lines != nullptr && lines[0] != nullptr ? lines[0] : "none");
    status = smith_run_combined(run, (const char *[]){"smith-no-such-program", nullptr}, &lines);
    fprintf(out, "missing %d lines %s\n", status, lines == nullptr ? "null" : "set");
    status = smith_run_combined(run, (const char *[]){"true", nullptr}, nullptr);
    fprintf(out, "true %d\n", status);
    fprintf(out, "status %d %d\n", smith_run_status(run, (const char *[]){"sh", "-c", "exit 4", nullptr}, nullptr),
            smith_run_status(run, (const char *[]){"sh", "-c", "kill -9 $$", nullptr}, nullptr));
    fclose(out);
    return 0;
}

void build(SmithBuild *b) {
    SmithStep step = {.run = probe, .outputs = (const char *[]){"combined.txt", nullptr}};
    smith_step(b, &step);
    smith_default(b, step.outputs);
}
C
build
expect_status "run_combined: the probe step succeeds" 0
expect "run_combined: lines in the order written, and the statuses" "sh 4 [o1] [e1] [o2] [e2] [o3]
killed 137 before
missing 127 lines null
true 0
status 4 137" "$(cat combined.txt)"

# 9s. A failed .run step names itself; what a .run step read reruns it.
project
cat >build.c <<'C'
#include <smith/build.h>
#include <stdio.h>

static int fail_three(SmithRun *run) {
    (void)run;
    fputs("the program's own message\n", stderr);
    return 3;
}

static int fail_unnamed(SmithRun *run) {
    (void)run;
    return 5;
}

// list.txt names the file to read; the step does not list that file as an input.
static int learn(SmithRun *run) {
    const char *const *lines = smith_lines(run->b, run->inputs[0]);
    smith_run_depend(run, lines[0]);
    FILE *log = fopen("learn.log", "a");
    fputs("ran\n", log);
    fclose(log);
    const char *const *data = smith_lines(run->b, lines[0]);
    smith_write(run->b, run->outputs[0], smith_fmt(run->b, "%s\n", data[0]));
    return 0;
}

static int learn_none(SmithRun *run) {
    smith_write(run->b, run->outputs[0], "none\n");
    return 0;
}

static int learn_orphan(SmithRun *run) {
    smith_run_depend(run, "x");
    return 0;
}

void build(SmithBuild *b) {
    SmithStep named = {.run = fail_three, .outputs = (const char *[]){"named.out", nullptr},
                       .description = "check the widget"};
    smith_step(b, &named);
    SmithStep unnamed = {.run = fail_unnamed, .outputs = (const char *[]){"unnamed.out", nullptr}};
    smith_step(b, &unnamed);
    SmithStep learn_step = {.run = learn,
                            .inputs = (const char *[]){"list.txt", nullptr},
                            .outputs = (const char *[]){"learned.out", nullptr},
                            .depfile = "build/learned.d"};
    smith_step(b, &learn_step);
    SmithStep none = {.run = learn_none, .outputs = (const char *[]){"none.out", nullptr}, .depfile = "build/none.d"};
    smith_step(b, &none);
    SmithStep orphan = {.run = learn_orphan, .stamp = "orphan.stamp"};
    smith_step(b, &orphan);
    smith_alias(b, "named", named.outputs);
    smith_alias(b, "unnamed", unnamed.outputs);
    smith_alias(b, "orphan", orphan.outputs);
    smith_default(b, smith_cat(b, learn_step.outputs, none.outputs, nullptr));
}
C
printf 'data file.txt\n' >list.txt
printf 'one\n' >'data file.txt'
build
expect "depend: the step ran and read the learned file" "0 one 1" "$rc $(cat learned.out 2>/dev/null) $(lines learn.log)"
build
expect_contains "depend: nothing changed, nothing reruns" "$out" "no work to do"
printf 'two\n' >'data file.txt'
build
expect "depend: a change to the learned file reruns the step" "two 2" "$(cat learned.out) $(lines learn.log)"
build
expect "depend: and then it settles" "2" "$(lines learn.log)"
printf 'data file.txt\n\n' >list.txt
build
expect "depend: a change to the input list reruns it too" "3" "$(lines learn.log)"
build named
expect_status "name: a failing .run step fails" fail
expect_contains "name: it prints its description and status" "$out$err" "smith: check the widget: failed (status 3)"
expect_contains "name: its own message still shows" "$out$err" "the program's own message"
build unnamed
expect_contains "name: with no description it names the first output" "$out$err" "smith: unnamed.out: failed (status 5)"
build orphan
expect_status "depend: recording a path without a .depfile fails" fail
expect_contains "depend: the message says why" "$out$err" "smith_run_depend: this step has no .depfile"

# 10. ninja arguments pass through (the copy project of case 1).
cd "$copy_project" || exit 1
build -t targets
expect_contains "ninja arguments: -t targets runs ninja's tool" "$out" "output"
rm output
build -n
expect "ninja arguments: -n does not build" "no" "$([ -e output ] && echo yes || echo no)"

# 11. Packages: a URL and a hash, fetched over file:// (the network is tested in tests/package.sh) into a cache of its own.
pk=$work/packages
mkdir -p "$pk/src/pkg-1.0/bin"
printf 'hello package\n' >"$pk/src/pkg-1.0/hello.txt"
printf '#!/bin/sh\necho tool ran\n' >"$pk/src/pkg-1.0/bin/tool"
chmod +x "$pk/src/pkg-1.0/bin/tool"
(cd "$pk/src" && tar -czf ../pkg.tar.gz pkg-1.0 && tar -cJf ../pkg.tar.xz pkg-1.0 && zip -qr ../pkg.zip pkg-1.0)
tar_url=file://$pk/pkg.tar.gz
tar_hash=sha256:$(shasum -a 256 "$pk/pkg.tar.gz" | cut -d' ' -f1)
tar_hex=${tar_hash#sha256:}
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *dir = smith_package(b, &(SmithPackage){
                                           .url = smith_option_value(b, "URL", nullptr, "the package"),
                                           .hash = smith_option_value(b, "HASH", nullptr, "its hash"),
                                       });
    smith_write(b, "path.txt", dir);
    SmithStep copy = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = smith_list(b, smith_fmt(b, "%s/hello.txt", dir), nullptr),
        .outputs = (const char *[]){"out.txt", nullptr},
    };
    smith_step(b, &copy);
    smith_default(b, copy.outputs);
}
C
export XDG_CACHE_HOME=$pk/cache
build -DURL="$tar_url" -DHASH="$tar_hash"
expect "package: the first build exits 0" 0 "$rc"
expect_contains "package: it says it is fetching" "$err" "fetching $tar_url"
expect "package: the path is in the cache, by hash" "$pk/cache/smith/packages/$tar_hex" "$(cat path.txt 2>/dev/null)"
expect "package: the archive's one top folder is stripped and its files are used" "hello package" "$(cat out.txt 2>/dev/null)"
expect "package: modes survive extraction" "tool ran" "$("$pk/cache/smith/packages/$tar_hex/bin/tool" 2>&1)"
expect "package: no temporary file is left in the cache" 0 "$(ls -A "$pk/cache/smith/packages" | grep -c '^\.tmp')"
mv "$pk/pkg.tar.gz" "$pk/pkg.tar.gz.away"
build -DURL="$tar_url" -DHASH="$tar_hash"
expect "package: a second build exits 0 with the archive gone" 0 "$rc"
expect_contains "package: a second build says nothing about fetching" "x$err" "x"
mv "$pk/pkg.tar.gz.away" "$pk/pkg.tar.gz"
build -DURL="$tar_url" -DHASH="$tar_hash" -DOFFLINE
expect "package: -DOFFLINE succeeds when it is cached" 0 "$rc"

wrong=sha256:$(printf 'f%.0s' $(seq 64))
build -DURL="$tar_url" -DHASH="$wrong"
expect_status "package: a wrong hash fails" fail
expect_contains "package: the message names the URL" "$err" "$tar_url"
expect_contains "package: and the expected hash" "$err" "expected $wrong"
expect_contains "package: and the actual hash" "$err" "actual   $tar_hash"
expect "package: a wrong hash leaves nothing under it" no "$([ -e "$pk/cache/smith/packages/${wrong#sha256:}" ] && echo yes || echo no)"
expect "package: and no temporary file" 0 "$(ls -A "$pk/cache/smith/packages" | grep -c '^\.tmp')"
build -DURL="$tar_url" -DHASH=sha256:abc
expect_status "package: a malformed hash fails" fail
expect_contains "package: the message says what a hash looks like" "$err" "64 hex digits"

# A fetch that fails (the file is not there) leaves no complete package and no temporary file; the next build is fine.
export XDG_CACHE_HOME=$pk/cache2
build -DURL="file://$pk/absent.tar.gz" -DHASH="$tar_hash"
expect_status "package: a failed fetch fails the build" fail
expect_contains "package: ... naming the URL" "$err" "file://$pk/absent.tar.gz"
expect "package: nothing is in the cache under the hash" no "$([ -e "$pk/cache2/smith/packages/$tar_hex" ] && echo yes || echo no)"
expect "package: and no temporary file is left" 0 "$(ls -A "$pk/cache2/smith/packages" | grep -c '^\.tmp')"
build -DURL="$tar_url" -DHASH="$tar_hash"
expect "package: the next build fetches it properly" "0 hello package" "$rc $(cat out.txt 2>/dev/null)"

# -DOFFLINE with an empty cache is an error that names the URL and fetches nothing.
export XDG_CACHE_HOME=$pk/cache3
build -DURL="$tar_url" -DHASH="$tar_hash" -DOFFLINE
expect_status "package: -DOFFLINE fails when the package is missing" fail
expect_contains "package: the message names the URL" "$err" "$tar_url"
expect "package: and makes no package" no "$([ -e "$pk/cache3/smith/packages/$tar_hex" ] && echo yes || echo no)"

# smith fetch prints the line to paste, and leaves the package in the cache.
capture "$smith" fetch "$tar_url"
expect "package: smith fetch prints the hash line" ".hash = \"$tar_hash\"," "$out"
expect "package: and leaves the package in the cache" "hello package" "$(cat "$pk/cache3/smith/packages/$tar_hex/hello.txt" 2>/dev/null)"
build -DURL="$tar_url" -DHASH="$tar_hash" -DOFFLINE
expect "package: a build offline then uses it" "0 hello package" "$rc $(cat out.txt 2>/dev/null)"
capture "$smith" fetch
expect "package: smith fetch needs a URL" 2 "$rc"
capture "$smith" fetch "file://$pk/missing.tar.gz"
expect_status "package: smith fetch of a missing file fails" fail

# .tar.xz extracts the same way, a zip is refused by name, and the XDG default is ~/.cache.
export XDG_CACHE_HOME=$pk/cache-xz
capture "$smith" fetch "file://$pk/pkg.tar.xz"
hash_line=$out
hash=${hash_line#*\"}
hash=${hash%%\"*}
expect "package: .tar.xz has the hash shasum gives" "sha256:$(shasum -a 256 "$pk/pkg.tar.xz" | cut -d' ' -f1)" "$hash"
expect "package: .tar.xz extracts" "hello package" "$(cat "$pk/cache-xz/smith/packages/${hash#sha256:}/hello.txt" 2>/dev/null)"
export XDG_CACHE_HOME=$pk/cache-zip
capture "$smith" fetch "file://$pk/pkg.zip"
expect_status "package: a .zip is refused" fail
expect_contains "package: and the message names the format and the archive" "$err" "zip archives are not supported"
expect "package: and nothing is left in the cache" 0 "$(ls "$pk/cache-zip/smith/packages" 2>/dev/null | grep -vc '^\.tmp\|prune.timestamp\|\.used')"
unset XDG_CACHE_HOME
mkdir "$pk/home"
HOME=$pk/home capture "$smith" fetch "$tar_url"
expect "package: with no XDG_CACHE_HOME the cache is ~/.cache" "hello package" \
    "$(cat "$pk/home/.cache/smith/packages/$tar_hex/hello.txt" 2>/dev/null)"
export XDG_CACHE_HOME=$work/xdg-cache

# SHA-256 against the NIST vectors (FIPS 180-2 examples, and the empty message), and shasum.
sha() { "$smith" --smith-sha256 "$1"; }
printf abc >"$pk/abc"
expect "sha256: \"abc\"" ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad "$(sha "$pk/abc")"
: >"$pk/empty"
expect "sha256: the empty message" e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 "$(sha "$pk/empty")"
printf abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq >"$pk/448"
expect "sha256: the 448-bit message" 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1 "$(sha "$pk/448")"
printf abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu >"$pk/896"
expect "sha256: the 896-bit message" cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1 "$(sha "$pk/896")"
head -c 1000000 /dev/zero | tr '\0' a >"$pk/million"
expect "sha256: a million \"a\"" cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0 "$(sha "$pk/million")"
head -c 300000 /dev/urandom >"$pk/random"
expect "sha256: 300000 random bytes equal shasum" "$(shasum -a 256 "$pk/random" | cut -d' ' -f1)" "$(sha "$pk/random")"
for size in 55 56 57 63 64 65 119 120 128; do # around the padding boundaries
    head -c "$size" /dev/urandom >"$pk/size"
    expect "sha256: $size bytes equal shasum" "$(shasum -a 256 "$pk/size" | cut -d' ' -f1)" "$(sha "$pk/size")"
done

# 11b. smith_modules: Clang modules, one module per header, maps in .smith/modules and the shared cache in $XDG_CACHE_HOME/smith/modules.
project
mkdir inc
printf '#pragma once\n#include <stdint.h>\nint a_value(void);\n' >inc/a.h
printf '#pragma once\n#include "a.h"\nint b_value(void);\n' >inc/b.h
printf '#pragma once\nint c_value(void);\n' >inc/c.h
printf '#include "inc/b.h"\n#include "inc/a.h"\n#include <stdio.h>\nint main(void) { printf("%%d\\n", b_value()); return 0; }\n' >x.c
printf '#include "inc/a.h"\n#include "inc/b.h"\nint a_value(void) { return 1; }\nint b_value(void) { return a_value() + 1; }\n' >ab.c
printf '#include "inc/c.h"\nint c_value(void) { return 3; }\n' >y.c
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_modules(b, &(SmithModules){.folders = (const char *[]){"inc", nullptr}});
    const char *const flags[] = {"-std=gnu23", "-Wall", "-Werror", nullptr};
    const char *const names[] = {"x", "ab", "y", nullptr};
    SmithObject objects[3];
    const SmithObject *linked[4] = {};
    for (int i = 0; names[i]; i++) {
        objects[i] = (SmithObject){.compiler = "cc", .source = smith_fmt(b, "%s.c", names[i]),
                                   .output = smith_fmt(b, "build/%s.o", names[i]),
                                   .flags = smith_cat(b, flags, smith_list(b, smith_option_value(b, "EXTRA", "-DNONE", "a flag"), nullptr), nullptr)};
        smith_object(b, &objects[i]);
        linked[i] = &objects[i];
    }
    SmithProgram app = {.linker = "cc", .output = "build/app", .objects = linked};
    smith_program(b, &app);
    smith_compile_commands(b, "compile_commands.json");
    smith_default(b, app.step.outputs);
}
C
build -v
expect "modules: the project builds" 0 "$rc"
expect_contains "modules: the compile has -fmodules" "$out" "cc -fmodules -fmodules-cache-path=$XDG_CACHE_HOME/smith/modules -fmodules-prune-interval=604800 -fmodules-prune-after=2678400 -fmodule-map-file=.smith/modules/inc.modulemap -fmodule-map-file=.smith/modules/smith.modulemap -std=gnu23"
expect "modules: the program runs" 2 "$(./build/app)"
case $(cat compile_commands.json) in *-fmodules-cache-path=*smith/modules*) expect "modules: compile_commands.json has the module flags the build uses" ok ok ;; *) expect "modules: compile_commands.json has the module flags the build uses" present none ;; esac
expect "modules: a map per folder and one for smith's headers" ".smith/modules/inc.modulemap .smith/modules/smith.modulemap" "$(echo .smith/modules/*.modulemap)"
expect_contains "modules: a header is a module of its own" "$(cat .smith/modules/inc.modulemap)" "module inc_b { header \"$(pwd -P)/inc/b.h\" export * }"
expect_contains "modules: smith's headers are modules" "$(cat .smith/modules/smith.modulemap)" "module smith_arena {"
deps=$("$smith" build -t deps build/y.o)
expect_contains "modules: the depfile lists the header y.c uses" "$deps" "inc/c.h"
case $deps in *inc/a.h* | *inc/b.h*) expect "modules: y.c depends on no other header" no yes ;; *) expect "modules: y.c depends on no other header" ok ok ;; esac
deps=$("$smith" build -t deps build/x.o)
expect_contains "modules: the depfile lists the module map" "$deps" ".smith/modules/inc.modulemap"
expect_contains "modules: the depfile lists b.h, which x.c includes" "$deps" "inc/b.h"
expect_contains "modules: the depfile lists a.h, which b.h includes" "$deps" "inc/a.h"
case $deps in *inc/c.h*) expect "modules: x.c does not depend on c.h" no yes ;; *) expect "modules: x.c does not depend on c.h" ok ok ;; esac
build
expect_contains "modules: a no-op rebuild does nothing" "$out" "no work to do"
sleep 1
echo '// edited' >>inc/c.h
build -v
expect_contains "modules: editing c.h rebuilds y.c" "$out" "-c y.c"
case $out in *"-c x.c"* | *"-c ab.c"*) expect "modules: editing c.h rebuilds only y.c" no yes ;; *) expect "modules: editing c.h rebuilds only y.c" ok ok ;; esac
sleep 1
echo '// edited' >>inc/a.h
build -v
expect_contains "modules: editing a.h rebuilds x.c, which reaches it through b.h" "$out" "-c x.c"
expect_contains "modules: editing a.h rebuilds ab.c" "$out" "-c ab.c"
case $out in *"-c y.c"*) expect "modules: editing a.h does not rebuild y.c" no yes ;; *) expect "modules: editing a.h does not rebuild y.c" ok ok ;; esac
expect "modules: the program still runs" 2 "$(./build/app)"
sleep 1
printf '#pragma once\nint d_value(void);\n' >inc/d.h
build -v
expect_contains "modules: a new header changes the map and rebuilds x.c" "$out" "-c x.c"
expect_contains "modules: a new header rebuilds y.c" "$out" "-c y.c"
build
expect_contains "modules: then a no-op again" "$out" "no work to do"
build -DEXTRA=-DOTHER -v
expect "modules: a flag change builds" 0 "$rc"
expect_contains "modules: a flag change recompiles" "$out" "-DOTHER"
expect "modules: the program runs after the flag change" 2 "$(./build/app)"
rm -rf build
build
expect "modules: the cache survives a clean of build/ and is reused" 0 "$rc"
expect_contains "modules: the cache is the shared one" "$(ls "$XDG_CACHE_HOME/smith/modules")" "modules.timestamp"
expect "modules: and nothing is cached under .smith" no "$([ -e .smith/cache ] && echo yes || echo no)"
if "$smith" lsp --version >/dev/null 2>&1; then
    capture "$smith" lsp --check="$(pwd -P)/x.c"
    errors=$(printf '%s\n' "$err" | grep -v 'tweak:' | grep 'E\[' || true)
    expect "modules: clangd reports no errors in x.c" "" "$errors"
fi

# 11c. Modules: the parallel build, ten clean builds in a row, 80 sources sharing the modules.
project
mkdir inc
for n in 1 2 3 4 5 6 7 8; do
    printf '#pragma once\n#include <stddef.h>\n#include <stdio.h>\nint h%s(int x);\n' "$n" >"inc/h$n.h"
done
printf '#pragma once\n#include "h1.h"\n#include "h2.h"\n#include "h3.h"\nint top(void);\n' >inc/top.h
for n in $(seq 1 80); do
    printf '#include "inc/top.h"\n#include "inc/h%s.h"\n#include "inc/h%s.h"\nint f%s(void) { return top() + h%s(%s); }\n' $((n % 8 + 1)) $(((n + 3) % 8 + 1)) "$n" $((n % 8 + 1)) "$n" >"s$n.c"
done
cat >build.c <<'C'
#include <smith/build.h>

#include <stddef.h>

void build(SmithBuild *b) {
    smith_modules(b, &(SmithModules){.folders = (const char *[]){"inc", nullptr}});
    const char *const sources = "s*.c";
    const char *const *files = smith_glob(b, smith_list(b, sources, nullptr));
    const char *const *objects = smith_map(b, files, "build/%s.o");
    for (size_t i = 0; files[i]; i++) {
        SmithObject o = {.compiler = "cc", .source = files[i], .output = objects[i], .flags = smith_list(b, "-std=gnu23", nullptr)};
        smith_object(b, &o);
    }
    smith_default(b, objects);
}
C
ok=0
for round in 1 2 3 4 5 6 7 8 9 10; do
    rm -rf build "$XDG_CACHE_HOME/smith/modules"
    build -j 16
    [ "$rc" -eq 0 ] && ok=$((ok + 1))
done
expect "modules: ten clean parallel builds with a cold cache succeed" 10 "$ok"

# 11d. Modules: isolation. A header that only worked after another include fails, and a textual header works.
project
mkdir inc
printf '#pragma once\nsize_t d_size(void);\n' >inc/needs_stddef.h
printf 'static int TEXTUAL_NAME(void) { return VALUE; }\n' >inc/twice.h
printf '#include <stddef.h>\n#include "inc/needs_stddef.h"\nint use(void) { return (int)d_size(); }\n' >bad.c
printf '#define VALUE 1\n#define TEXTUAL_NAME one\n#include "inc/twice.h"\n#undef TEXTUAL_NAME\n#define TEXTUAL_NAME two\n#include "inc/twice.h"\n' >twice.c
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_modules(b, &(SmithModules){.folders = (const char *[]){"inc", nullptr}, .textual = smith_list(b, smith_option_value(b, "TEXTUAL", nullptr, "a header"), nullptr)});
    const char *source = smith_option_value(b, "SOURCE", "bad.c", "the source");
    SmithObject o = {.compiler = "cc", .source = source, .output = "build/o.o", .flags = smith_list(b, "-std=gnu23", nullptr)};
    smith_object(b, &o);
    smith_default(b, o.step.outputs);
}
C
build
expect_status "modules: a header that needed an earlier include fails" fail
expect_contains "modules: clang says which module and which line" "$out" "unknown type name 'size_t'"
expect_contains "modules: and that it was building the module" "$out" "While building module 'inc_needs_stddef'"
build -DSOURCE=twice.c
expect_status "modules: a header included twice with different macros, not marked, fails to be a module" fail
build -DSOURCE=twice.c -DTEXTUAL=inc/twice.h
expect "modules: marked textual, it builds" 0 "$rc"
expect_contains "modules: the textual header is in a module that only names it" "$(cat .smith/modules/inc.modulemap)" "textual header"
build -DSOURCE=twice.c -DTEXTUAL=inc/nothere.h
expect_status "modules: a textual header no folder holds is an error" fail
expect_contains "modules: the error names it" "$err" "inc/nothere.h"

# 11e. Modules: sanitized and plain builds keep their own modules, and a Metal file never gets the flags.
project
mkdir inc
printf '#pragma once\nint v(void);\n' >inc/v.h
printf '#include "inc/v.h"\nint v(void) { return 4; }\n' >p.c
printf '#include "inc/v.h"\nint v(void);\nint main(void) { return v() - 4; }\n' >q.c
printf 'kernel void k() {}\n' >k.metal
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_modules(b, &(SmithModules){.folders = (const char *[]){"inc", nullptr}});
    SmithObject plain = {.compiler = "cc", .source = "p.c", .output = "build/plain.o", .flags = smith_list(b, "-std=gnu23", nullptr)};
    SmithObject asan = {.compiler = "cc", .source = "q.c", .output = "build/asan.o", .flags = smith_list(b, "-std=gnu23", "-fsanitize=address,undefined", nullptr)};
    SmithObject metal = {.compiler = "echo", .source = "k.metal", .output = "build/k.o"};
    smith_object(b, &plain);
    smith_object(b, &asan);
    smith_object(b, &metal);
    SmithProgram app = {.linker = "cc", .output = "build/app", .flags = smith_list(b, "-fsanitize=address,undefined", nullptr), .objects = (const SmithObject *[]){&plain, &asan, nullptr}};
    smith_program(b, &app);
    smith_compile_commands(b, "compile_commands.json");
    smith_default(b, app.step.outputs);
}
C
build
expect "modules: a plain and a sanitized object build" 0 "$rc"
expect "modules: the program runs" 0 "$(./build/app >/dev/null 2>&1; echo $?)"
hashes=$(find "$XDG_CACHE_HOME/smith/modules" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')
[ "$hashes" -ge 2 ] && expect "modules: the two builds have caches of their own" ok ok || expect "modules: the two builds have caches of their own" ">= 2" "$hashes"
capture "$smith" build -t compdb
expect_contains "modules: ninja's compile commands carry the flags" "$out" "-fmodules"
case $(cat compile_commands.json 2>/dev/null) in *-fmodules-cache-path=*smith/modules*) expect "modules: compile_commands.json has the module flags the build uses" ok ok ;; *) expect "modules: compile_commands.json has the module flags the build uses" present none ;; esac
metal_line=$(printf '%s\n' "$out" | grep 'k.metal' | head -1)
case $metal_line in *-fmodules*) expect "modules: the .metal compile has no module flags" none "$metal_line" ;; *) expect "modules: the .metal compile has no module flags" ok ok ;; esac

# 11f. Modules: errors.
project
mkdir inc
printf '#pragma once\n' >inc/a.h
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithObject o = {.compiler = "cc", .source = "a.c", .output = "build/a.o"};
    smith_object(b, &o);
    smith_modules(b, &(SmithModules){.folders = (const char *[]){"inc", nullptr}});
}
C
printf 'int a;\n' >a.c
build
expect_status "modules: after smith_object is an error" fail
expect_contains "modules: the message" "$err" "smith_modules is called after smith_object"
for folders in '"inc", "inc/sub"' '"nothing"' '"."' '"../inc"'; do
    cat >build.c <<C
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_modules(b, &(SmithModules){.folders = (const char *[]){$folders, nullptr}});
    smith_default(b, smith_list(b, nullptr));
}
C
    mkdir -p inc/sub
    build
    expect_status "modules: the folders $folders are refused" fail
    expect_contains "modules: $folders: the message starts smith: build.c: smith_modules" "$err" "smith: build.c: smith_modules"
done

# 11g. Modules: every header of smith's own libraries imports alone and together (io includes the arena and random).
project
for header in arena random io test build; do
    printf '#include <smith/%s.h>\n' "$header" >"only_$header.c"
done
printf '#include <smith/io.h>\n#include <smith/arena.h>\n#include <smith/random.h>\n#include <smith/test.h>\n#include <smith/build.h>\n' >all_of_them.c
cat >build.c <<'C'
#include <smith/build.h>

#include <stddef.h>

void build(SmithBuild *b) {
    smith_modules(b, &(SmithModules){});
    const char *const *files = smith_glob(b, smith_list(b, "*_*.c", nullptr));
    const char *const *objects = smith_map(b, files, "build/%s.o");
    for (size_t i = 0; files[i]; i++) {
        SmithObject o = {.compiler = "cc", .source = files[i], .output = objects[i],
                         .flags = smith_list(b, "-std=gnu23", "-Wall", "-Werror", "-I", ".smith/include", nullptr)};
        smith_object(b, &o);
    }
    smith_compile_commands(b, "compile_commands.json");
    smith_default(b, objects);
}
C
build -v
expect "modules: smith's headers import alone and together" 0 "$rc"
expect_contains "modules: the smith map is used" "$out" "-fmodule-map-file=.smith/modules/smith.modulemap"
expect_contains "modules: the library map has io" "$(cat .smith/modules/smith.modulemap)" "module smith_io {"
if "$smith" lsp --version >/dev/null 2>&1; then
    # compile_commands.json carries the module flags, and smith lsp (clangd, no --query-driver) builds the modules.
    expect_contains "modules: compile_commands.json names the smith map" "$(cat compile_commands.json)" "smith.modulemap"
    capture "$smith" lsp --check="$(pwd -P)/all_of_them.c"
    errors=$(printf '%s\n' "$err" | grep -v 'tweak:' | grep 'E\[' || true)
    expect "modules: clangd reports no errors in a file that imports smith's headers" "" "$errors"
fi

# 14. The executor (smith build runs the manifest itself). Manifests written by hand run through
# `smith --smith-exec <manifest>`, which reads the same language and behaves as ninja did.
exec_run() { capture "$smith" --smith-exec build.ninja "$@"; }

# 14a. A failed job: the status, the outputs, the command and what it printed; the first failure stops the build.
project
cat >build.ninja <<'N'
rule r
  command = $cmd
build bad: r
  cmd = echo to-out; echo to-err >&2; exit 3
build good: r
  cmd = echo ran >> good.log; echo G > good
build after: r bad
  cmd = echo ran >> after.log; echo A > after
default bad good after
N
exec_run -j1
expect "failure: the build fails" 1 "$rc"
expect_contains "failure: FAILED names the status and the output" "$out" "FAILED: [code=3] bad "
expect_contains "failure: then the command" "$out" "echo to-out; echo to-err >&2; exit 3"
expect_contains "failure: then what it printed, stdout and stderr in order" "$(printf '%s' "$out" | tr '\n' ' ')" "to-out to-err"
expect_contains "failure: the build says why it stopped" "$err" "build stopped: subcommand failed."
expect "failure: the first failure stops the others" "no no" "$([ -e good ] && echo yes || echo no) $([ -e after ] && echo yes || echo no)"
rm -f good
exec_run -j1 -k 0
expect "keep going: the build still fails" 1 "$rc"
expect "keep going: -k 0 builds what does not depend on the failure" "G" "$(cat good 2>/dev/null)"
expect "keep going: and not what does" no "$([ -e after ] && echo yes || echo no)"
expect_contains "keep going: it says it cannot go on" "$err" "cannot make progress due to previous errors."
rm -f good
exec_run -j1 -k 2
expect "keep going: -k 2 stops after the second failure, there is only one" "G" "$(cat good 2>/dev/null)"
exec_run -j
expect_contains "options: -j needs a number" "$err" "needs a value"
exec_run -jx
expect_contains "options: -jx is not a number" "$err" "needs a number"
exec_run --bogus
expect_contains "options: an unknown flag is refused" "$err" "unknown option"

# 14b. Depfiles: what the command read is kept, and the file is removed.
project
cat >build.ninja <<'N'
rule cc
  command = $cmd
  depfile = $out.d
  deps = gcc
build obj: cc src
  cmd = echo ran >> runs; cp src obj; echo "obj: src hdr hdr\\ one.h" > obj.d
default obj
N
echo s >src
echo h >hdr
echo h >'hdr one.h'
exec_run
expect "depfile: the first build runs" 1 "$(lines runs)"
expect "depfile: the depfile is removed once read" no "$([ -e obj.d ] && echo yes || echo no)"
exec_run -t deps obj
expect_contains "depfile: -t deps lists what it named" "$out" "    hdr one.h"
expect_contains "depfile: and says it is valid" "$out" "(VALID)"
exec_run
expect_contains "depfile: an unchanged tree has no work" "$out" "no work to do"
sleep 0.05
touch hdr
exec_run
expect "depfile: a changed header reruns the step" 2 "$(lines runs)"
sleep 0.05
touch 'hdr one.h'
exec_run
expect "depfile: a header with a space in its name is read" 3 "$(lines runs)"
exec_run
expect "depfile: and settles" 3 "$(lines runs)"
rm hdr
exec_run
expect "depfile: a header that is gone reruns the step once" 4 "$(lines runs)"
cat >build.ninja <<'N'
rule cc
  command = $cmd
  depfile = $out.d
  deps = gcc
build obj: cc src
  cmd = echo ran >> runs; cp src obj; echo "wrong: src" > obj.d
default obj
N
exec_run
expect_status "depfile: a depfile that names another output fails the step" fail
expect_contains "depfile: and says what it expected" "$out" "expected depfile 'obj.d' to mention 'obj', got 'wrong'"

# 14c. restat: an output that did not change cleans what depended on it, and the time kept is the input's.
project
cat >build.ninja <<'N'
rule step
  command = $cmd
build gen: step seed
  cmd = echo gen >> gen.log; head -n 1 seed > gen.new; (cmp -s gen.new gen && rm gen.new) || mv gen.new gen
  restat = 1
build use: step gen
  cmd = echo use >> use.log; cp gen use
build last: step use
  cmd = echo last >> last.log; cp use last
default last
N
printf 'one\ntwo\n' >seed
exec_run
expect "restat: the first build runs the chain" "1 1 1" "$(lines gen.log) $(lines use.log) $(lines last.log)"
sleep 0.05
printf 'one\nthree\n' >seed
exec_run
expect "restat: an input change reruns the step, but not what the unchanged output feeds" "2 1 1" "$(lines gen.log) $(lines use.log) $(lines last.log)"
exec_run
expect_contains "restat: and the next build has nothing to do" "$out" "no work to do"
printf 'uno\nthree\n' >seed
exec_run
expect "restat: a changed output reruns everything after it" "3 2 2" "$(lines gen.log) $(lines use.log) $(lines last.log)"
exec_run -n
expect_contains "dry run: a settled tree says so" "$out" "no work to do"
touch seed
exec_run -n
expect_contains "dry run: lists the steps it would run, the dependents included" "$out" "[1/3] echo gen >> gen.log"
expect "dry run: and runs nothing" 3 "$(lines gen.log)"
exec_run
expect "restat: an input touched after the output is checked again" "4 2 2" "$(lines gen.log) $(lines use.log) $(lines last.log)"
exec_run
expect_contains "restat: and then settles, the log has the input's time" "$out" "no work to do"

# 14d. Jobs run at once and print whole; a pool serialises its jobs; a console job keeps stdin.
project
cat >build.ninja <<'N'
pool one
  depth = 1
rule step
  command = $cmd
rule single
  command = $cmd
  pool = one
build a: step
  cmd = echo a1; sleep 0.5; echo a2; echo a3 >&2
build b: step
  cmd = echo b1; sleep 0.1; echo b2
default a b
N
exec_run -j2
flat=$(printf '%s' "$out" | tr '\n' ' ')
expect_contains "parallel: a job's output is printed whole" "$flat" "a1 a2 a3"
expect_contains "parallel: and so is the other's" "$flat" "b1 b2 "
case $flat in *b1\ b2*a1\ a2*) expect "parallel: the one that ends first prints first" ok ok ;; *) expect "parallel: the one that ends first prints first" "b before a" "$flat" ;; esac
expect "parallel: the progress line counts jobs as they end" "[1/2] [2/2]" "$(printf '%s' "$out" | grep -o '^\[[0-9]/[0-9]\]' | tr '\n' ' ' | sed 's/ $//')"
cat >build.ninja <<'N'
pool one
  depth = 1
rule step
  command = $cmd
rule single
  command = $cmd
  pool = one
build a: single
  cmd = echo start >> order; sleep 0.2; echo end >> order
build b: single
  cmd = echo start >> order; sleep 0.2; echo end >> order
default a b
N
exec_run -j4
expect "pool: a pool of depth 1 runs one job at a time" "start end start end" "$(tr '\n' ' ' <order | sed 's/ $//')"
rm -f order a b
sed -i.bak 's/  pool = one//' build.ninja
exec_run -j4
expect "pool: without it the two run together" "start start end end" "$(tr '\n' ' ' <order | sed 's/ $//')"
cat >build.ninja <<'N'
rule step
  command = $cmd
  pool = console
build ask: step
  cmd = read line; echo "got $$line" > answer; echo "terminal: $$line"
default ask
N
out=$(printf 'hello\n' | "$smith" --smith-exec build.ninja 2>"$work/err")
rc=$?
expect "console: the job reads the build's own stdin" "0 got hello" "$rc $(cat answer 2>/dev/null)"
expect_contains "console: and writes straight to its stdout" "$out" "terminal: hello"

# 14e. Ctrl-C (SIGINT) stops the jobs, removes what they half wrote and exits with 130.
project
cat >build.ninja <<'N'
rule step
  command = $cmd
build slow: step
  cmd = echo partial > slow; echo started >> started; sleep 33
build slow2: step
  cmd = echo started >> started; sleep 33
default slow slow2
N
"$smith" --smith-exec build.ninja >"$work/o" 2>"$work/e" &
pid=$!
for _ in $(seq 1 100); do
    [ "$(lines started)" = 2 ] && break
    sleep 0.05
done
t0=$(perl -MTime::HiRes=time -e 'printf "%.3f", time')
kill -INT "$pid"
wait "$pid"
rc=$?
t1=$(perl -MTime::HiRes=time -e 'printf "%.3f", time')
expect "interrupt: the build exits with 130" 130 "$rc"
expect_contains "interrupt: and says why" "$(cat "$work/e")" "interrupted by user"
expect "interrupt: both jobs were stopped" 0 "$(pgrep -f 'sleep 33' | wc -l | tr -d ' ')"
expect "interrupt: the half-written output is removed" no "$([ -e slow ] && echo yes || echo no)"
expect "interrupt: and it was quick" yes "$(perl -e "print(($t1 - $t0) < 2 ? 'yes' : 'no')")"
sed -i.bak 's/sleep 33/true/' build.ninja
exec_run
expect "interrupt: the next build runs both again" "0 2" "$rc $(lines started | awk '{print $1 - 2}')"

# 14f. The tools, and the manifests the executor refuses.
project
cat >build.ninja <<'N'
builddir = .smith
rule step
  command = $cmd
rule cc
  command = $cmd
  depfile = $out.d
  deps = gcc
rule regen
  command = $cmd
  generator = 1
build build.ninja: regen
  cmd = true
build dir/one.o: cc one.c
  cmd = cp one.c dir/one.o; echo "dir/one.o: one.c" > dir/one.o.d
build app: step dir/one.o
  cmd = cp dir/one.o app
build all: phony app
default all
N
echo c >one.c
exec_run
expect "output folders: made before the command runs" "c" "$(cat dir/one.o 2>/dev/null)"
exec_run -t targets
expect_contains "tools: targets lists the roots" "$out" "all: phony"
exec_run -t targets all
expect_contains "tools: targets all lists every output with its rule" "$out" "dir/one.o: cc"
exec_run -t commands app
expect "tools: commands lists them in order" "cp one.c dir/one.o; echo \"dir/one.o: one.c\" > dir/one.o.d
cp dir/one.o app" "$out"
exec_run -t compdb cc
expect_contains "tools: compdb prints the JSON compilation database" "$out" '"file": "one.c"'
expect_contains "tools: compdb names the output" "$out" '"output": "dir/one.o"'
expect_contains "tools: compdb names the directory" "$out" "\"directory\": \"$(pwd -P)\""
exec_run -t clean
expect_contains "tools: clean says how many it removed" "$out" "Cleaning... 2 files."
expect "tools: clean removed the outputs" "no no" "$([ -e app ] && echo yes || echo no) $([ -e dir/one.o ] && echo yes || echo no)"
expect "tools: and kept the manifest, a generator's output" yes "$([ -f build.ninja ] && echo yes)"
exec_run
exec_run -t clean dir/one.o
expect "tools: clean of one target removes only what it needs" "yes no" "$([ -e app ] && echo yes || echo no) $([ -e dir/one.o ] && echo yes || echo no)"
exec_run
sed -i.bak '/^build app/,/^  cmd = cp dir/d' build.ninja
sed -i.bak 's/^build all: phony app/build all: phony dir\/one.o/' build.ninja
exec_run -t cleandead
expect_contains "tools: cleandead removes what the manifest no longer makes" "$out" "Cleaning... 1 files."
expect "tools: and only that" "no yes" "$([ -e app ] && echo yes || echo no) $([ -e dir/one.o ] && echo yes || echo no)"
exec_run -t nothing
expect_contains "tools: an unknown tool names the known ones" "$err" "unknown tool 'nothing'"

for refusal in "cycle|build a: step b
  cmd = true
build b: step a
  cmd = true
default a|dependency cycle: a -> b -> a" "missing|build x: step nofile
  cmd = true
default x|'nofile', needed by 'x', missing and no known rule to make it" "twice|build x: step
  cmd = true
build x: step
  cmd = true
default x|multiple rules generate x" "rule|build x: nothing
default x|unknown build rule" "target|build x: step
  cmd = true
default x|unknown target 'nothere'"; do
    body=${refusal#*|}
    name=${refusal%%|*}
    expected=${body##*|}
    manifest=${body%|*}
    printf 'rule step\n  command = $cmd\n%s\n' "$manifest" >build.ninja
    if [ "$name" = target ]; then exec_run nothere; else exec_run; fi
    expect_contains "manifest: $name is refused with its reason" "$err" "$expected"
    expect "manifest: ... and the build fails" 1 "$rc"
done

# 14g. What is kept between builds: a ninja tree is rebuilt once, a damaged log is never trusted.
project
cat >build.ninja <<'N'
builddir = .smith
rule step
  command = $cmd
build out: step in
  cmd = echo ran >> runs; cp in out
default out
N
echo in >in
exec_run
exec_run
expect "state: the second build has nothing to do" 1 "$(lines runs)"
expect_contains "state: the log has its header" "$(head -n 1 .smith/.smith_log)" "# smith log 1"
expect "state: and a line for the output" 1 "$(grep -c ' out$' .smith/.smith_log)"
printf '0 0 0 0123456789abcdef ou' >>.smith/.smith_log
exec_run
expect "state: a line cut short is not read" 1 "$(lines runs)"
printf '# smith log 2\n' >.smith/.smith_log
exec_run
expect "state: a log of another version is not trusted" 2 "$(lines runs)"
exec_run
expect "state: and is written again" 2 "$(lines runs)"
rm .smith/.smith_log
exec_run
expect "state: no log means every command runs" 3 "$(lines runs)"
sed -i.bak 's/echo ran/echo ran again/' build.ninja
exec_run
expect "state: a changed command reruns the step" 4 "$(lines runs)"
rm out
exec_run
expect "state: a deleted output is built again" 5 "$(lines runs)"
sleep 0.05
touch in
exec_run
expect "state: a newer input is built again" 6 "$(lines runs)"
sleep 0.05
touch out
exec_run
expect "state: an output newer than its input is left alone" 6 "$(lines runs)"
if ninja --version >/dev/null 2>&1; then
    # A tree that ninja built has its own log, which smith does not read: the first smith build runs everything.
    project
    cp "$copy_project/build.c" .
    echo one >input
    capture env SMITH_NINJA="$(command -v ninja)" "$smith" build
    expect "ninja: SMITH_NINJA runs the manifest with ninja" "0 one" "$rc $(cat output)"
    expect "ninja: and ninja's own log is written" yes "$([ -f .smith/.ninja_log ] && echo yes)"
    expect "ninja: and smith's is not" no "$([ -f .smith/.smith_log ] && echo yes || echo no)"
    echo two >input
    build
    expect "upgrade: the first smith build of a tree ninja built runs the command" "0 two" "$rc $(cat output)"
    build
    expect_contains "upgrade: then it has nothing to do" "$out" "no work to do"
    echo three >input
    capture env SMITH_NINJA="$(command -v ninja)" "$smith" build
    expect "upgrade: ninja can take over again, and builds what changed" "0 three" "$rc $(cat output)"
fi

# 12. A smith built with one command.
project
capture "$cc" -std=gnu23 "$here/../smith.c" -o "$work/one-command-smith"
expect "one command: cc -std=gnu23 smith.c builds a smith" 0 "$rc"
capture "$work/one-command-smith" version
expect_contains "one command: the built smith runs" "$out" "smith 0."

# 13. A change of compiler rebuilds every object it compiled; a build with the same compiler does nothing.
# ninja sees the same command (`smith ...`) before and after, so the objects would look up to date. Each object and
# program depends on a file in .smith/compiler/ that says which compiler it is (see build_runtime.c).
project
mkdir bin
for name in cc1 cc2 cc3; do
    # A stand-in compiler: it writes the file after -o, saying who wrote it, and counts its runs in ./runs.
    cat >"bin/$name" <<SH
#!/bin/sh
while [ \$# -gt 0 ]; do
    if [ "\$1" = -o ]; then out=\$2; fi
    shift
done
echo "$name" >"\$out"
echo "\$0" >>"$(pwd)/runs"
SH
    chmod +x "bin/$name"
done
printf 'int main(void) { return 0; }\n' >a.c
printf 'int f(void) { return 1; }\n' >b.c
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *compiler = smith_option_value(b, "COMPILER", "bin/cc1", "the compiler");
    SmithObject a = {.compiler = compiler, .source = "a.c", .output = "build/a.o"};
    SmithObject other = {.compiler = compiler, .source = "b.c", .output = "build/b.o"};
    smith_object(b, &a);
    smith_object(b, &other);
    SmithProgram program = {.linker = "bin/cc3", .output = "build/program", .objects = (const SmithObject *[]){&a, &other, nullptr}};
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
}
C
build
expect "compiler change: the first build runs the compiler on both sources and the linker" 3 "$(lines runs)"
build
expect_contains "compiler change: a build with the same compiler does nothing" "$out" "no work to do"
expect "compiler change: and ran nothing" 3 "$(lines runs)"
build -DCOMPILER=bin/cc2
expect "compiler change: another compiler rebuilds both objects and links again" 6 "$(lines runs)"
expect "compiler change: the object is the new compiler's" cc2 "$(cat build/a.o)"
build -DCOMPILER=bin/cc2
expect_contains "compiler change: the new compiler's build does nothing too" "$out" "no work to do"
build -DCOMPILER=bin/cc1
expect "compiler change: going back rebuilds as well" 9 "$(lines runs)"
expect "compiler change: the object is the old compiler's again" cc1 "$(cat build/a.o)"
# The same compiler replaced by another file: a new version.
printf '#!/bin/sh\nwhile [ $# -gt 0 ]; do\n    if [ "$1" = -o ]; then out=$2; fi\n    shift\ndone\necho cc1-new >"$out"\necho "$0" >>"%s/runs"\n' "$(pwd)" >bin/cc1.new
chmod +x bin/cc1.new
mv bin/cc1.new bin/cc1
build
expect "compiler change: the same path with another compiler in it rebuilds both objects" 12 "$(lines runs)"
expect "compiler change: and the objects are the new one's" cc1-new "$(cat build/a.o)"
build
expect_contains "compiler change: and then settles" "$out" "no work to do"

# smith as the compiler: what it runs is part of what it is. The external clangs are wrappers that note their runs on a.c
# and run the real clang (smith build compiles build.c with them as well, which they let pass).
real_clang=$(command -v cc)
for name in w2 w3; do
    cat >"bin/$name" <<SH
#!/bin/sh
case " \$* " in *" a.c "*) echo "$name" >>"$(pwd)/wrapped" ;; esac
exec $real_clang "\$@"
SH
    chmod +x "bin/$name"
done
mkdir smith-bin
cp "$smith" smith-bin/smith-cc
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithObject a = {.compiler = "smith-bin/smith-cc", .source = "a.c", .output = "build/a.o"};
    smith_object(b, &a);
    smith_default(b, a.step.outputs);
}
C
rm -rf build runs
capture env SMITH_CLANG="$(pwd)/bin/w2" "$smith" build
expect "smith routes to an external clang: the first build runs it" w2 "$(cat wrapped)"
capture env SMITH_CLANG="$(pwd)/bin/w2" "$smith" build
expect_contains "smith routes to an external clang: the same clang does nothing" "$out" "no work to do"
capture env SMITH_CLANG="$(pwd)/bin/w3" "$smith" build
expect "smith routes to an external clang: SMITH_CLANG changed to another clang rebuilds the object" "$(printf 'w2\nw3')" "$(cat wrapped)"
capture env SMITH_CLANG="$(pwd)/bin/w2" "$smith" build
expect "smith routes to an external clang: and back again" "$(printf 'w2\nw3\nw2')" "$(cat wrapped)"
printf '#!/bin/sh\ncase " $* " in *" a.c "*) echo w2new >>"%s/wrapped" ;; esac\nexec %s "$@"\n' "$(pwd)" "$real_clang" >bin/w2.new
chmod +x bin/w2.new
mv bin/w2.new bin/w2
capture env SMITH_CLANG="$(pwd)/bin/w2" "$smith" build
expect "smith routes to an external clang: a clang replaced by another in the same place rebuilds" \
    "$(printf 'w2\nw3\nw2\nw2new')" "$(cat wrapped)"

# The compiler is the clang smith runs, not smith's file: a smith replaced by a copy (a rebuilt smith) compiles the same.
rm -rf build runs
capture env -u SMITH_CLANG "$smith" build
expect_contains "smith upgrade: the first build compiles the object" "$out" "a.c"
expect "smith upgrade: with the real compiler" 0 "$rc"
build
expect_contains "smith upgrade: the second build does nothing" "$out" "no work to do"
cp "$smith" smith-bin/smith-cc.new
mv smith-bin/smith-cc.new smith-bin/smith-cc
build
expect_contains "smith upgrade: a replaced smith with the same clang rebuilds nothing" "$out" "no work to do"
capture env SMITH_CLANG="$(pwd)/bin/w3" "$smith" build
expect_contains "smith upgrade: the same smith switched to an external clang rebuilds the object" "$out" "a.c"
capture env -u SMITH_CLANG "$smith" build
expect_contains "smith upgrade: and back to the clang inside rebuilds it again" "$out" "a.c"
capture env -u SMITH_CLANG "$smith" build
expect_contains "smith upgrade: and then settles" "$out" "no work to do"

# What a smith with clang inside says about its clang is its identity: a rebuilt smith reports the same, a rebuilt LLVM
# another. A stand-in smith that prints the two lines shows both (the real one is covered above and by run.sh).
mkdir fake
cat >fake/smith-cc <<'SH'
#!/bin/sh
case $1 in --smith-which) printf 'clang in-process /llvm/bin/clang\nidentity %s\n' "$(cat "$0.id")" ;; *) echo "$0" >>runs ;; esac
while [ $# -gt 0 ]; do if [ "$1" = -o ]; then : >"$2"; fi; shift; done
SH
chmod +x fake/smith-cc
echo "llvm-23.1.3 aaaa" >fake/smith-cc.id
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithObject a = {.compiler = "fake/smith-cc", .source = "a.c", .output = "build/a.o"};
    smith_object(b, &a);
    smith_default(b, a.step.outputs);
}
C
rm -rf build runs
build
expect "identity: the first build compiles" 1 "$(lines runs)"
cp fake/smith-cc fake/smith-cc.new
mv fake/smith-cc.new fake/smith-cc
build
expect_contains "identity: a smith rebuilt with the same LLVM rebuilds nothing" "$out" "no work to do"
echo "llvm-23.1.3 bbbb" >fake/smith-cc.id
build
expect "identity: a smith linked against another LLVM rebuilds the object" 2 "$(lines runs)"
build
expect_contains "identity: and then settles" "$out" "no work to do"

# 15. .smith modules: smith_object writes each module's header first, then compiles with -I .smith/include (docs/smith-lang.md).
# A project of three modules and a C file that includes a module's header.
lang_project() { # copies the corpus case module-three and writes its build.c
    project
    mkdir bin
    ln -f "$smith" bin/smith-cc 2>/dev/null || cp "$smith" bin/smith-cc
    cp -R "$here/smith-lang/cases/module-three/src" src
    cat >build.c <<'C'
#include <smith/build.h>

#include <stddef.h>
#include <string.h>

static SmithObject objects[64];
static const SmithObject *pointers[65];

void build(SmithBuild *b) {
    const char *const flags[] = {"-std=gnu23", "-g0", "-Wno-unused-function", nullptr};
    const char *const *sources = smith_glob(b, (const char *[]){"src/**/*.smith", "src/*.c", nullptr});
    size_t count = 0;
    for (; sources[count] != nullptr; count++) {
        objects[count] = (SmithObject){
            .compiler = "bin/smith-cc",
            .source = sources[count],
            .output = smith_fmt(b, "build/%s.o", sources[count]),
            .flags = flags,
        };
        if (strcmp(sources[count], "src/checksum.c") == 0) { // C reads the header of a module
            objects[count].flags = smith_cat(b, flags, smith_list(b, "-I", ".smith/include", nullptr), nullptr);
            objects[count].implicit_inputs = smith_list(b, ".smith/include/game/replay.h", nullptr);
        }
        smith_object(b, &objects[count]);
        pointers[count] = &objects[count];
    }
    SmithProgram app = {.linker = "bin/smith-cc", .output = "build/app", .objects = pointers};
    smith_program(b, &app);
    smith_compile_commands(b, "compile_commands.json");
    smith_default(b, app.step.outputs);
}
C
}

# What a -v build ran: the objects it compiled, as a sorted list of the sources.
lang_compiled() {
    printf '%s\n%s\n' "$out" "$err" | sed -n 's/.* -c \(src\/[^ ]*\) -o .*/\1/p' | sort | tr '\n' ' '
}

lang_project
if "$smith" cc --smith-which -x c 2>/dev/null | grep -q "token-hook yes"; then
    expected_output=$(printf 'frames=15 name=last.replay length=11 version=3 mode=1 root=4.0 eof=-1\ncounter=8\nchecksum=1026')
    build
    expect "modules: a project of three modules and a C file builds" 0 "$rc"
    expect "modules: and runs" "$expected_output" "$(./build/app 2>&1)"
    expect "modules: each module has its header in .smith/include" "yes yes" \
        "$([ -f .smith/include/lib/text.h ] && echo yes) $([ -f .smith/include/game/replay.h ] && echo yes)"
    expect_contains "modules: a header lists the modules it imports" "$(cat .smith/include/game/replay.h)" \
        "// smith-imports: lib::text"
    expect_contains "modules: compile_commands.json lists a .smith source with -I .smith/include" "$(cat compile_commands.json)" \
        '"file": "src/game/replay.smith"'
    expect_contains "modules: with its flags, and the language clangd is told" "$(tr -d ' \n' <compile_commands.json)" \
        '"arguments":["bin/smith-cc","-x","c","-I",".smith/include","-std=gnu23"'
    build
    expect_contains "modules: the second build does nothing" "$out" "no work to do"

    stat -f %m .smith/include/lib/text.h >header_time
    sleep 1
    printf '\nusize other_helper(void) {\n    return 5;\n}\n' >>src/lib/text.smith
    build -v
    expect "modules: an edit to a module's body recompiles that module alone" "src/lib/text.smith " "$(lang_compiled)"
    expect "modules: and leaves its header's time alone" "$(cat header_time)" "$(stat -f %m .smith/include/lib/text.h)"
    expect "modules: and the program still runs" "$expected_output" "$(./build/app 2>&1)"

    sleep 1
    printf '\nexport usize extra(void) {\n    return 2;\n}\n' >>src/game/replay.smith
    build -v
    expect "modules: an edit to a module's exports recompiles it and its importers, and the C file that includes the header" \
        "src/checksum.c src/game/replay.smith src/main.smith " "$(lang_compiled)"
    expect "modules: and the program still runs after an export is added" "$expected_output" "$(./build/app 2>&1)"
    build
    expect_contains "modules: then it settles" "$out" "no work to do"

    sleep 1
    printf '\n// a comment\n' >>src/main.smith
    build -v
    expect "modules: an edit to an importer's body recompiles it alone" "src/main.smith " "$(lang_compiled)"

    expect_contains "modules: the header steps are in the manifest" "$(cat .smith/build.ninja)" \
        "bin/smith-cc --smith-header src/lib/text.smith -o .smith/include/lib/text.h"

    # An import cycle is found from the headers, before any compile starts, and named.
    lang_project
    rm -rf src
    mkdir -p src
    cp -R "$here/smith-lang/cases/error-import-cycle/src/cycle" src/cycle
    build
    expect_status "modules: an import cycle fails the build" fail
    expect_contains "modules: and names the cycle" "$out$err" "import cycle: cycle::a -> cycle::b -> cycle::a"
    expect "modules: before any compile runs" "" "$(ls build/src/cycle 2>/dev/null)"

    # A module whose name does not match its path is found once, when build.c runs.
    lang_project
    printf 'module right::name;\n\nexport int f(void) { return 1; }\n' >src/wrong.smith
    build
    expect_status "modules: a module that does not match its path fails the build" fail
    expect_contains "modules: and says where it must be" "$err" \
        "src/wrong.smith declares module right::name, so its path must end in right/name.smith"

    # A plain .smith file (no module line) compiles with the same flags and needs no header.
    lang_project
    rm -rf src
    mkdir -p src
    cp "$here/smith-lang/cases/sized-types/src/main.smith" src/main.smith
    build
    expect "modules: a plain .smith file builds, with no header" "0 no" "$rc $([ -e .smith/include/main.h ] && echo yes || echo no)"
else
    # A smith without the token hook (a stage0) still writes headers; the compile says what is missing.
    build
    expect_status "modules: without the token hook the build fails" fail
    expect_contains "modules: with a clear message" "$out$err" "token hook"
    expect "modules: after the headers were written" "lib/text.h" "$(cd .smith/include && ls lib/text.h)"
fi

# The helpers are under the compiler; the top level is only verbs.
lang_project
capture "$smith" cc --smith-header src/lib/text.smith -o header.h
expect "modules: smith cc --smith-header writes a header" "0 // smith-module: lib::text" "$rc $(sed -n 2p header.h)"
capture "$smith" cc --smith-header src/lib/text.smith -o header.h
cp header.h header.copy
touch -t 200001010000 header.h
capture "$smith" cc --smith-header src/lib/text.smith -o header.h
expect "modules: an unchanged header is not written again" "200001010000" "$(stat -f %Sm -t %Y%m%d%H%M header.h)"
capture "$smith" --smith-header src/lib/text.smith -o header.h
expect_status "modules: the top-level --smith-header is gone" fail
expect_contains "modules: with the way to run it" "$err" "smith cc --smith-header"
capture "$smith" --smith-lower src/lib/text.smith
expect_status "modules: the top-level --smith-lower is gone" fail
capture "$smith" cc --smith-lower src/lib/text.smith
expect_contains "modules: smith cc --smith-lower prints the lowering" "$out" "lib_text_make"

# 13z. smith_cat: a nullptr first list would end the arguments and drop the other lists, so it is an error that says so.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *selected = nullptr;
    const char *const *joined = smith_cat(b, selected, (const char *[]){"--function=x", nullptr}, nullptr);
    smith_write(b, "joined.txt", smith_join(b, joined, " "));
}
C
build
expect_status "smith_cat: a nullptr first list fails" fail
expect_contains "smith_cat: ... as a build.c error that says why" "$err" "smith: build.c: smith_cat: the first list is nullptr"
expect_contains "smith_cat: ... and what to write instead" "$err" "(const char *[]){nullptr}"
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *selected = (const char *[]){nullptr};
    const char *const *joined = smith_cat(b, selected, (const char *[]){"--function=x", nullptr}, nullptr);
    smith_write(b, "joined.txt", smith_join(b, joined, " "));
}
C
build
expect "smith_cat: an empty first list keeps the others" "--function=x" "$(cat joined.txt 2>/dev/null)"

# 13y. A compiler named smith, by name, by path or from the environment (CC=$HOME/.local/bin/smith), is refused before
# anything runs, with the hint to use smith-cc or smith-c++.
project
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithObject app = {.compiler = smith_option_value(b, "CC", smith_env(b, "CC", "smith-cc"), "the compiler"),
                       .source = "main.c", .output = "main.o"};
    smith_object(b, &app);
    smith_default(b, app.step.outputs);
}
C
echo 'int main(void) { return 0; }' >main.c
capture env CC="$smith" "$smith" build
expect_status "CC=<path>/smith in the environment is refused" fail
expect_contains "CC=<path>/smith: ... as a build.c error naming the value" "$err" "smith: build.c: smith_object: .compiler is \"$smith\""
expect_contains "CC=<path>/smith: ... with the hint for C" "$err" "use \"$(dirname "$smith")/smith-cc\" for C"
expect_contains "CC=<path>/smith: ... and for C++" "$err" "smith-c++\" for C++"
expect "CC=<path>/smith: nothing ran" "" "$(ls main.o .smith/compiler 2>/dev/null)"
capture "$smith" build -DCC=smith
expect_status "-DCC=smith is refused" fail
expect_contains "-DCC=smith: ... with the hint" "$err" "use \"smith-cc\" for C"
capture env CC=smith-cc "$smith" build
expect "CC=smith-cc still builds" 0 "$rc"

# 14. A no-op build, timed (informational).
cd "$copy_project" || exit 1
build
start=$(perl -MTime::HiRes=time -e 'printf "%.6f", time')
for _ in 1 2 3 4 5 6 7 8 9 10; do "$smith" build >/dev/null 2>&1; done
end=$(perl -MTime::HiRes=time -e 'printf "%.6f", time')
echo "time  no-op smith build: $(perl -e "printf '%.0f', ($end - $start) * 100") ms per run (mean of 10)"

echo
echo "$total build checks, $failures failed"
[ "$failures" -eq 0 ]
