#!/bin/sh
# End-to-end tests for build.smith and `smith build --dump-config` (issue #43, part 3): a project's build file written in
# .smith with `import smith::build;`, the verbs on it, and the build file that does what the conventions do.
# Usage: sh tests/build-smith.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
mkdir "$work/bin" "$work/home"
ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" && ln -s "$smith" "$work/bin/smith-c++"
PATH=$work/bin:$PATH
export HOME=$work/home
export XDG_CACHE_HOME=$work/xdg-cache # the module and package caches of the tests, never the real ones
export XDG_DATA_HOME=$work/xdg-data

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

last() { printf '%s\n' "$1" | tail -n 1; } # the last line: the program's output follows the build's

exists() { if [ -e "$1" ] || [ -L "$1" ]; then echo yes; else echo no; fi; }

# The files of the program: main.smith imports geometry::box, a test in tests/.
sources() { # folder
    mkdir -p "$1/geometry" "$1/tests"
    cat >"$1/main.smith" <<'EOF'
module main;
import geometry::box;
import <stdio.h>;

i32 main(void) {
    i32 x = box::area(3, 4);
    c::printf("%d\n", x);
    return 0;
}
EOF
    cat >"$1/geometry/box.smith" <<'EOF'
module geometry::box;

export i32 area(i32 w, i32 h) {
    return w * h;
}
EOF
    cat >"$1/tests/test_box.smith" <<'EOF'
import smith::test;
import geometry::box;

void test_area(void) {
    test::check_equal(box::area(2, 3), 6);
}
EOF
}

# 1. A build.smith of the high-level calls builds, runs and tests.
sources "$work/shapes"
cd "$work/shapes" || exit 1
cat >build.smith <<'EOF'
import smith::build;

void build(build::Build *b) {
    build::app(b, &(build::App){.entry = "main.smith"});
    build::app_tests(b, &(build::AppTests){.folder = "tests"});
}
EOF
capture smith build
expect "build.smith: builds" 0 "$rc"
expect "build.smith: ... the debug program is build/debug/<folder>" yes "$(exists build/debug/shapes)"
expect "build.smith: ... smith's conventions are not used" no "$(exists .smith/conventions.c)"
expect_contains "build.smith: the compile command of the build file is in .smith/build.info" "$(cat .smith/build.info)" \
    "compile	build.smith"
expect_contains "build.smith: ... and in the compile commands" "$(cat .smith/compile_commands.json)" '"file": "build.smith"'
capture smith build
expect "build.smith: a second build has nothing to do" "smith: no work to do." "$out"
capture smith run
expect "build.smith: smith run runs the program" "12" "$(last "$out")"
capture smith test
expect "build.smith: smith test passes" 0 "$rc"
expect_contains "build.smith: ... its test" "$out$err" "1 passed, 0 failed"
capture smith prove
expect "build.smith: smith prove runs" 0 "$rc"
expect_contains "build.smith: ... and says there is nothing to prove" "$out" "nothing to prove"
capture smith build release fast
expect "build.smith: the other profiles build" 0 "$rc"
expect "build.smith: ... and run" "12" "$(build/release/shapes)"
cd "$work" || exit 1

# 2. A build.smith with a low-level step beside the high-level calls, and one with only low-level calls.
sources "$work/mixed"
cd "$work/mixed" || exit 1
cat >build.smith <<'EOF'
import smith::build;

void build(build::Build *b) {
    build::app(b, &(build::App){});
    build::app_tests(b, &(build::AppTests){});
    build::task(b, "greet", &(build::Step){
        .command = build::list(b, "echo", build::fmt(b, "hello from %s", b->os), nullptr),
        .description = "greeting",
    });
}
EOF
capture smith build greet
expect "low-level: a task in a build.smith runs" 0 "$rc"
expect_contains "low-level: ... and prints" "$out" "hello from $(uname -s)"
capture smith test
expect "low-level: the tests of the high-level calls still pass" 0 "$rc"
mkdir "$work/plain" && cd "$work/plain" || exit 1
printf 'int main(void) {\n    return 7;\n}\n' >main.c
cat >build.smith <<'EOF'
import smith::build;

void build(build::Build *b) {
    build::Object object = {
        .compiler = "smith-cc",
        .source = "main.c",
        .output = "build/main.o",
        .flags = build::list(b, "-O1", "-Wall", nullptr),
    };
    build::object(b, &object);
    build::Program program = {
        .linker = "smith-cc",
        .output = "build/plain",
        .objects = (const build::Object *const[]){&object, nullptr},
    };
    build::program(b, &program);
    build::default_targets(b, program.step.outputs);
}
EOF
capture smith build
expect "low-level: only low-level calls: builds" 0 "$rc"
build/plain
expect "low-level: ... the program is the C file's" 7 "$?"
capture smith build -t commands build/main.o
expect_contains "low-level: ... compiled with the flags it was given" "$out" " -O1 -Wall "
cd "$work" || exit 1

# 3. build.smith beside build.c is an error that names both.
mkdir "$work/both" && cd "$work/both" || exit 1
printf 'int main(void) {\n    return 0;\n}\n' >main.c
printf '#include <smith/build.h>\n\nvoid build(SmithBuild *b) {\n    smith_app(b, &(SmithApp){});\n}\n' >build.c
printf 'import smith::build;\n\nvoid build(build::Build *b) {\n    build::app(b, &(build::App){});\n}\n' >build.smith
capture smith build
expect "both: build.smith and build.c: status 2" 2 "$rc"
expect_contains "both: ... names both" "$err" "both build.c and build.smith are here"
for verb in run test; do
    capture smith $verb
    expect "both: smith $verb says the same" 2 "$rc"
done
cd "$work" || exit 1

# 4. The stage0 has no hook, so it refuses a build.smith and says what is needed.
cc -std=gnu23 "$here/smith.c" -o "$work/stage0" 2>"$work/stage0.err"
expect "stage0: builds with one cc command" 0 "$?"
mkdir "$work/zero" && cp "$work/shapes/build.smith" "$work/zero/build.smith" && cd "$work/zero" || exit 1
capture "$work/stage0" build
expect "stage0: refuses build.smith: status 2" 2 "$rc"
expect_contains "stage0: ... says the full smith is needed" "$err" "build.smith needs the full smith"
expect "stage0: ... and compiles nothing" no "$(exists .smith/program)"
capture "$work/stage0" build --dump-config
expect "stage0: --dump-config of a folder with a build file: status 0" 0 "$rc"
cd "$work" || exit 1

# 5. --dump-config of a .smith project: a build.smith that builds as the conventions do.
sources "$work/dump"
cd "$work/dump" || exit 1
capture smith build -t commands build/debug/dump build/release/dump build/fast/dump build/tests
conventional=$out
capture smith build --dump-config
expect "dump: a .smith project: status 0" 0 "$rc"
printf '%s\n' "$out" >"$work/dumped.smith"
expect_contains "dump: ... is a build.smith of the high-level calls" "$out" "import smith::build;"
expect_contains "dump: ... names the entry" "$out" '.entry = "main.smith",'
expect_contains "dump: ... and the program" "$out" '.name = "dump",'
expect_contains "dump: ... and the tests folder" "$out" 'build::app_tests(b, &(build::AppTests){.folder = "tests"});'
expect "dump: ... writes only the file to stdout" "" "$err"
expect "dump: ... and nothing to the folder" no "$(exists build.smith)"
cp "$work/dumped.smith" build.smith
rm -rf .smith build
capture smith build -t commands build/debug/dump build/release/dump build/fast/dump build/tests
expect "dump: the build.smith gives the commands of no build file" "$conventional" "$out"
capture smith build
expect "dump: ... builds" 0 "$rc"
# It failed once under the load of a full smith check and never alone: show what the build said, to find out why.
[ "$rc" -eq 0 ] || printf '  stdout: %s\n  stderr: %s\n' "$out" "$err"
capture smith run
expect "dump: ... runs" "12" "$(last "$out")"
capture smith test
expect_contains "dump: ... and tests" "$out$err" "1 passed, 0 failed"
capture smith build --dump-config
expect "dump: a project with a build.smith: status 0" 0 "$rc"
expect "dump: ... prints nothing to stdout" "" "$out"
expect_contains "dump: ... says the file is already explicit" "$err" "build.smith is already this project's build file"
cd "$work" || exit 1

# 6. --dump-config of a C project: a build.c through <smith/build.h>.
mkdir "$work/cdump" && cd "$work/cdump" || exit 1
mkdir tests
cat >main.c <<'EOF'
#include "util.h"

#include <stdio.h>

int main(void) {
    printf("%d\n", util_twice(21));
    return 0;
}
EOF
printf '#pragma once\nint util_twice(int x);\n' >util.h
printf '#include "util.h"\n\nint util_twice(int x) {\n    return x * 2;\n}\n' >util.c
cat >tests/test_util.c <<'EOF'
#include "util.h"

#include <smith/test.h>

void test_twice(void) {
    smith_check(util_twice(2) == 4);
}
EOF
capture smith build -t commands build/debug/cdump build/release/cdump build/fast/cdump build/tests
conventional=$out
capture smith build --dump-config
expect "dump: a C project: status 0" 0 "$rc"
printf '%s\n' "$out" >"$work/dumped.c"
expect_contains "dump: ... is a build.c" "$out" "#include <smith/build.h>"
expect_contains "dump: ... of smith_app" "$out" 'smith_app(b, &(SmithApp){'
expect_contains "dump: ... with the entry" "$out" '.entry = "main.c",'
expect_contains "dump: ... and the tests" "$out" 'smith_app_tests(b, &(SmithAppTests){.folder = "tests"});'
cp "$work/dumped.c" build.c
rm -rf .smith build
capture smith build -t commands build/debug/cdump build/release/cdump build/fast/cdump build/tests
expect "dump: the build.c gives the commands of no build file" "$conventional" "$out"
capture smith test
expect_contains "dump: ... and tests" "$out$err" "1 passed, 0 failed"
capture smith build --dump-config
expect "dump: a project with a build.c: status 0" 0 "$rc"
expect "dump: ... prints nothing to stdout" "" "$out"
expect_contains "dump: ... says the file is already explicit" "$err" "build.c is already this project's build file"
cd "$work" || exit 1

# 7. --dump-config of a folder that follows no convention is the guidance error, and of both entry points is an error.
mkdir "$work/nothing" && cd "$work/nothing" || exit 1
capture smith build --dump-config
expect "dump: nothing to build: status 2" 2 "$rc"
expect "dump: ... prints nothing to stdout" "" "$out"
expect_contains "dump: ... says what smith looked for" "$err" "no main.smith or main.c to build by convention"
mkdir "$work/twice" && cd "$work/twice" || exit 1
printf 'int main(void) {\n    return 0;\n}\n' >main.c
printf 'int main(void) {\n    return 0;\n}\n' >main.smith
capture smith build --dump-config
expect "dump: main.smith and main.c: status 2" 2 "$rc"
expect_contains "dump: ... names both" "$err" "both main.smith and main.c are here"
cd "$work" || exit 1

# 8. smith lint lints a build.smith as written, as it lints a build.c.
cd "$work/shapes" || exit 1
cp "$here/.clang-format" .
# smith's own checks, but not smith-effects: this program prints with printf, and the lint here is about build.smith.
sed 's/^  smith-effects,$/  -smith-effects,/' "$here/.clang-tidy" >.clang-tidy
capture smith lint
expect "lint: a clean build.smith passes" 0 "$rc"
[ "$rc" -eq 0 ] || printf '  lint said: %s\n' "$out$err"
printf '\nstatic int BadName(int x) {\n    return x;\n}\n' >>build.smith
capture smith lint
expect "lint: a finding in build.smith fails" 1 "$rc"
expect_contains "lint: ... at its line, in the name as written" "$out$err" "build.smith:8:12"
cd "$work" || exit 1

# smith check reads a build.smith too: with no build.c and no main, it still lints and tests.
mkdir "$work/checked" && cd "$work/checked" || exit 1
printf 'int main(void) {\n    return 0;\n}\n' >prog.c
cat >build.smith <<'EOF'
import smith::build;

void build(build::Build *b) {
    build::app(b, &(build::App){.entry = "prog.c"});
}
EOF
capture smith check
case $err in
*"are skipped"*) ok=no ;;
*"smith check: lint "*"smith check: test "*) ok=yes ;;
*) ok=no ;;
esac
expect "check: a build.smith with no main is linted and tested" yes "$ok"
cd "$work" || exit 1

# 10. A mistake in a build.smith names build.smith, not build.c, and every name collision is reported in one run (#47).
mkdir "$work/errors" && cd "$work/errors" || exit 1
printf 'int main(void) {\n    return 0;\n}\n' >prog.c
cat >build.smith <<'EOF'
import smith::build;

void build(build::Build *b) {
    build::Step step = {.command = build::list(b, "true", nullptr)};
    build::task(b, "lint", &step);
    build::alias(b, "fmt", step.outputs);
    build::Step other = {.command = build::list(b, "true", nullptr)};
    build::task(b, "release", &other);
    build::app(b, &(build::App){.entry = "prog.c"});
}
EOF
capture smith build
expect "errors: a build.smith with mistakes fails" 2 "$rc"
expect_contains "errors: the message names build.smith" "$err" "smith: build.smith: task 'lint' is named after a verb"
expect_contains "errors: the second collision is reported in the same run" "$err" "smith: build.smith: alias 'fmt' is named after a verb"
expect_contains "errors: the third, an alias the app makes against a task" "$err" \
    "smith_app: the alias release that every program adds to is the name of a task; name the task another way"
case $err in
*"build.c"*) named=build.c ;;
*) named=none ;;
esac
expect "errors: and never says build.c" none "$named"
cat >build.smith <<'EOF'
import smith::build;

void build(build::Build *b) {
    build::app(b, &(build::App){.entry = "prog.c"});
}
EOF
capture smith build -Dnope
expect_contains "errors: an unknown option names build.smith" "$err" "smith: build.smith: unknown option -Dnope"
expect_contains "errors: and says it declares none" "$err" "build.smith declares no options"
cd "$work" || exit 1

# 11. Relaxing a convention's warning (smith-build-levels): the example there builds, and without the flags it fails.
mkdir "$work/relax" && cd "$work/relax" || exit 1
mkdir fixtures
printf '// a fixture with nothing in it\n' >fixtures/empty.c
printf '#include <assert.h>\n#include "fixtures/empty.c"\n\nstatic_assert(0.5f + 0.5f == 1.0f, "exact");\n\nint main(void) {\n    return 0;\n}\n' >main.c
cat >build.smith <<'EOF'
import smith::build;

void build(build::Build *b) {
    build::app(b, &(build::App){.entry = "main.c", .name = "hello"});
}
EOF
capture smith build
expect "relax: without the flags the conventions' warnings fail the build" 1 "$rc"
expect_contains "relax: the diagnostic names the flag to turn off" "$out$err" "[-Werror,-Wgnu-folding-constant]"
cat >build.smith <<'EOF'
import smith::build;

void build(build::Build *b) {
    build::defaults(b, &(build::Defaults){
        .folders = (const build::Folder[]){{.path = "fixtures", .flags = build::list(b, "-Wno-empty-translation-unit", nullptr)}, {}},
    });
    build::app(b, &(build::App){
        .entry = "main.c",
        .name = "hello",
        .flags = build::list(b, "-Wno-gnu-folding-constant", nullptr),
    });
}
EOF
capture smith build
expect "relax: with -Wno-... in .flags and a folder's list it builds" 0 "$rc"
cd "$work" || exit 1

echo
echo "$total build.smith checks, $failures failed"
[ "$failures" -eq 0 ]
