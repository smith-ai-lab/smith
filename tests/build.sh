#!/bin/sh
# End-to-end tests for `smith build`, with real ninja and the real clang smith picks. Each case runs in its own
# temporary project. Usage: sh tests/build.sh path/to/smith
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
expect "newline: a second build changes nothing and keeps the file" "ninja: no work to do. 0" \
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

# 9m. Editing the function reruns the step; options reach the function through the replay; defer.
project
cat >build.c <<'C'
#include <smith/build.h>

static int write_it(SmithRun *run) {
    smith_write(run->b, run->outputs[0], smith_fmt(run->b, "v1 %s\n", smith_option_value(run->b, "who", "nobody", nullptr)));
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
    smith_option_value(b, "who", "nobody", "a name");
    SmithStep step = {.run = write_it, .outputs = (const char *[]){"it.out", nullptr}};
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
expect "options: -Dname=value reaches the function through the replay" "0 v2 Ada" "$rc $(cat it.out 2>/dev/null)"
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

# 9o. The replay's cost (informational): a trivial .run task against a trivial command task.
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
