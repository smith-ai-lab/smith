#!/bin/sh
# End-to-end tests for builds by convention (issue #43): a folder with no build file and a main.smith or main.c builds,
# runs and tests, with smith_app and smith_app_tests as its build program. Usage: sh tests/conventions.sh path/to/smith
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

# The files a compile of the build lists in .smith/build.info, as a line.
compiled() { grep '^compile	' "$1/.smith/build.info" | cut -f2 | grep -v '^build.c$' | tr '\n' ' ' | sed 's/ $//'; }

# 1. A .smith project: main imports geometry::box, which imports geometry::scale; one more module nothing imports.
mkdir "$work/shapes" && cd "$work/shapes" || exit 1
mkdir geometry tests
cat >main.smith <<'EOF'
module main;
import geometry::box;
import <stdio.h>;

int main() {
    i32 x = box::area(3, 4);
    c::printf("%d\n", x);
    return 0;
}
EOF
cat >geometry/box.smith <<'EOF'
module geometry::box;
import geometry::scale;

export i32 area(i32 w, i32 h) {
    return scale::times(w, h);
}
EOF
cat >geometry/scale.smith <<'EOF'
module geometry::scale;

export i32 times(i32 a, i32 b) {
    return a * b;
}
EOF
cat >geometry/unused.smith <<'EOF'
module geometry::unused;

export i32 nothing() {
    return 0 +; // would not compile: nothing imports this file, so nothing compiles it
}
EOF
cat >tests/test_box.smith <<'EOF'
import <smith/test.h>;
import geometry::box;

void test_area(void) {
    c::smith_check_equal_i32(box::area(2, 3), 6);
}
EOF
capture smith build
expect "smith: builds with no build file" 0 "$rc"
expect "smith: the debug program is build/debug/<folder>" yes "$(exists build/debug/shapes)"
expect "smith: the project gets no build file" no "$(exists build.c)"
expect "smith: the build program is smith's, in .smith" yes "$(exists .smith/conventions.c)"
expect "smith: the files of the program, found by their imports" "main.smith geometry/box.smith geometry/scale.smith tests/test_box.smith" \
    "$(compiled .)"
expect "smith: a module nothing imports is not compiled" no "$(exists build/debug/geometry/unused.o)"
expect "smith: ... nor is its header written" no "$(exists .smith/include/geometry/unused.h)"
expect "smith: the compile commands are in .smith" yes "$(exists .smith/compile_commands.json)"
expect "smith: ... and not at the root" no "$(exists compile_commands.json)"
capture smith build
expect "smith: a second build has nothing to do" "smith: no work to do." "$out"
capture smith run
expect "smith run: runs the program" "12" "$(last "$out")"
capture smith test
expect "smith test: passes" 0 "$rc"
expect_contains "smith test: ran the test" "$out$err" "1 passed, 0 failed"
capture smith build release fast
expect "smith build release fast: builds" 0 "$rc"
expect "smith: the release program" yes "$(exists build/release/shapes)"
expect "smith: the fast program" yes "$(exists build/fast/shapes)"
expect "smith: the release program runs" "12" "$(build/release/shapes)"
expect "smith: the fast program runs" "12" "$(build/fast/shapes)"
capture smith build -t commands build/release/geometry/box.o
expect_contains "smith: release has -O2" "$out" " -O2 "
case $out in *-DNDEBUG*) ndebug=yes ;; *) ndebug=no ;; esac
expect "smith: release keeps assertions on" no "$ndebug"
capture smith build -t commands build/fast/geometry/box.o
expect_contains "smith: fast has -DNDEBUG" "$out" "-DNDEBUG"
capture smith build -t commands build/debug/geometry/box.o
expect_contains "smith: debug has the sanitizers" "$out" "-fsanitize=address,undefined"
expect_contains "smith: ... and -std=gnu2y -Werror" "$out" "-std=gnu2y"
# A module added to the program is found on the next build, with no change to a build file.
cat >geometry/volume.smith <<'EOF'
module geometry::volume;
import geometry::box;

export i32 of(i32 w, i32 h, i32 d) {
    return box::area(w, h) * d;
}
EOF
cat >main.smith <<'EOF'
module main;
import geometry::volume;
import <stdio.h>;

int main() {
    c::printf("%d\n", volume::of(2, 3, 4));
    return 0;
}
EOF
capture smith run
expect "smith: a new import is followed on the next build" "24" "$(last "$out")"
expect "smith: ... through the modules it imports" yes "$(exists build/debug/geometry/scale.o)"
cd "$work" || exit 1

# 2. A C project: main.c includes util.h (util.c beside it), which includes deep/x.h (deep/x.c); a stray .c nothing includes.
mkdir "$work/calc" && cd "$work/calc" || exit 1
mkdir deep tests
cat >main.c <<'EOF'
#include "util.h"

#include <stdio.h>

int main(void) {
    printf("%d\n", util_twice(21));
    return 0;
}
EOF
cat >util.h <<'EOF'
#pragma once
int util_twice(int x);
EOF
cat >util.c <<'EOF'
#include "util.h"

#include "deep/x.h"

int util_twice(int x) {
    return deep_id(x) * 2;
}
EOF
cat >deep/x.h <<'EOF'
#pragma once
int deep_id(int x);
EOF
cat >deep/x.c <<'EOF'
#include "x.h"

int deep_id(int x) {
    return x;
}
EOF
cat >stray.c <<'EOF'
int stray(void) { return 1 +; } // nothing includes stray.h, so nothing compiles this
EOF
cat >tests/test_util.c <<'EOF'
#include "util.h"

#include <smith/test.h>

void test_twice(void) {
    smith_check(util_twice(2) == 4);
}
EOF
capture smith build
expect "c: builds with no build file" 0 "$rc"
expect "c: the files of the program, found by their includes" "main.c util.c deep/x.c tests/test_util.c" "$(compiled .)"
expect "c: a .c nothing includes is not compiled" no "$(exists build/debug/stray.o)"
expect "c: the objects keep the folders" yes "$(exists build/debug/deep/x.o)"
capture smith run
expect "c: runs" "42" "$(last "$out")"
capture smith test
expect "c: the tests pass" 0 "$rc"
expect_contains "c: ... one of them" "$out$err" "1 passed, 0 failed"
capture smith prove
expect "c: smith prove runs on it" 0 "$rc"
expect_contains "c: ... and says there is nothing to prove" "$out" "nothing to prove"
capture smith prove --function=no_such_function
expect "c: smith prove --function= naming nothing fails" 1 "$rc"
expect_contains "c: ... and says no source defines it" "$err" "--function: no function by that name in any source"
cp "$here/.clang-tidy" "$here/.clang-format" .
capture smith lint
expect "c: smith lint reads the build's compiles" 0 "$rc"
# A comment is not an include, and a block comment spans lines.
cat >util.h <<'EOF'
#pragma once
// #include "stray.h"
/*
#include "stray.h"
*/
int util_twice(int x);
EOF
printf 'int stray_value(void);\n' >stray.h
printf 'int stray_value(void) {\n    return 1;\n}\n' >stray.c
capture smith build
expect "c: includes in comments are not followed" no "$(exists build/debug/stray.o)"
cd "$work" || exit 1

# The same two calls in a build.c are the same build: the same commands for every target.
mkdir -p "$work/explicit" && cp -R "$work/calc" "$work/explicit/calc" && cd "$work/explicit/calc" || exit 1
rm -rf .smith build
cat >build.c <<'EOF'
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_app(b, &(SmithApp){});
    smith_app_tests(b, &(SmithAppTests){});
}
EOF
capture smith build -t commands build/debug/calc build/release/calc build/fast/calc build/tests
explicit_commands=$out
capture smith test
expect "explicit: the two calls in a build.c: smith test passes" 0 "$rc"
rm build.c
rm -rf .smith build
capture smith build -t commands build/debug/calc build/release/calc build/fast/calc build/tests
expect "explicit: ... give the commands of no build file" "$explicit_commands" "$out"
cd "$work" || exit 1

# 3. smith's libraries need no declaration.
mkdir "$work/ioc" && cd "$work/ioc" || exit 1
cat >main.c <<'EOF'
#include <smith/io.h>

#include <stdio.h>

int main(int argc, char **argv) {
    SmithIo io = smith_io_make(argc, argv);
    printf("seed %d\n", io.seed != 0);
    smith_io_close(&io);
    return 0;
}
EOF
capture smith run
expect "library: #include <smith/io.h> links io" "seed 1" "$(last "$out")"
expect "library: ... and the libraries io needs" "arena.o contract.o io.o math.o random.o trace.o" "$(ls build/debug/smith | tr '\n' ' ' | sed 's/ $//')"
cd "$work" || exit 1
mkdir "$work/textual" && cd "$work/textual" || exit 1
cat >main.smith <<'EOF'
import smith::text;
import <stdio.h>;

int main(void) {
    const char word[:] = "42";
    u64 value = 0;
    if (!text::parse_u64(word, &value)) return 1;
    c::printf("%llu\n", (unsigned long long)value);
    return 0;
}
EOF
capture smith run
expect "library: import smith::text links text" "42" "$(last "$out")"
cd "$work" || exit 1
mkdir "$work/iosmith" && cd "$work/iosmith" || exit 1
cat >main.smith <<'EOF'
import <smith/io.h>;
import <stdio.h>;

int main(int argc, char **argv) {
    c::SmithIo io = c::smith_io_make(argc, argv);
    c::printf("hello\n");
    c::smith_io_close(&io);
    return 0;
}
EOF
capture smith run
expect "library: import <smith/io.h>; in a .smith file links io" "hello" "$(last "$out")"
# A library used only by a test is built for the tests and not linked into the program.
mkdir "$work/tonly" && cd "$work/tonly" || exit 1
mkdir tests
printf 'int main(void) {\n    return 0;\n}\n' >main.c
cat >tests/test_clock.c <<'EOF'
#include <smith/io.h>
#include <smith/test.h>

void test_io_makes(void) {
    SmithIo io = smith_io_make(0, nullptr);
    smith_check(io.seed != 0);
    smith_io_close(&io);
}
EOF
capture smith test
expect "library: a test that includes <smith/io.h> links io" 0 "$rc"
capture smith build -t commands build/debug/tonly
case $out in *smith/io.o*) linked=yes ;; *) linked=no ;; esac
expect "library: ... and the program does not" no "$linked"
cd "$work" || exit 1

# 4. A folder that follows no convention.
mkdir "$work/make" && cd "$work/make" || exit 1
printf 'all:\n\tcc -o app app.c\n' >Makefile
capture smith build
expect "error: a Makefile and no main: status 2" 2 "$rc"
expect_contains "error: ... says the Makefile is the build" "$err" "smith: this folder has a Makefile and no build.c or build.smith"
expect_contains "error: ... and how to translate it" "$err" "Translate it with \`smith build --dump-config > build.c\`"
expect_contains "error: ... or to build by convention" "$err" "delete it to build by convention"
mkdir "$work/empty" && cd "$work/empty" || exit 1
capture smith build
expect "error: an empty folder: status 2" 2 "$rc"
expect_contains "error: ... says what smith looked for" "$err" "smith: no build.c in this folder, and no main.smith or main.c to build by convention"
expect_contains "error: ... and how to start a build file" "$err" "Add a main.smith or main.c (\`smith new\` writes one)"
cd "$work/make" || exit 1
expect "error: ... and guesses nothing" no "$(exists .smith/program)"
for verb in run test; do
    capture smith $verb
    expect "error: smith $verb says the same" 2 "$rc"
done
mkdir "$work/both" && cd "$work/both" || exit 1
printf 'int main(void) {\n    return 0;\n}\n' >main.c
printf 'int main(void) {\n    return 0;\n}\n' >main.smith
capture smith build
expect "error: both main.smith and main.c: status 2" 2 "$rc"
expect_contains "error: ... names both" "$err" "both main.smith and main.c are here"
mkdir "$work/plain" && cd "$work/plain" || exit 1
printf 'int main(void) {\n    return 0;\n}\n' >main.c
capture smith test
expect "test: a project with no tests says so" 0 "$rc"
expect_contains "test: ... in smith test's words" "$err" "the build declares no tests"
cd "$work" || exit 1

# 6. Several programs, the defaults every program shares, folders' flags, includes found through -I, helpers in the tests.
mkdir -p "$work/multi" && cd "$work/multi" || exit 1
mkdir -p src/base tools tests
cat >src/base/value.h <<'EOF'
#pragma once
int base_value(void);
EOF
cat >src/base/value.c <<'EOF'
#include "base/value.h"
#include "build/gen.h"

int base_value(void) {
    return 3 * GEN_VALUE;
}
EOF
cat >main.c <<'EOF'
#include <stdio.h>

#include "base/value.h"

int main(void) {
    printf("%d\n", base_value());
    return 0;
}
EOF
cat >tools/tool.c <<'EOF'
#include <stdio.h>

#include "base/value.h"

int main(void) {
    printf("tool %d\n", 2 * base_value());
    return 0;
}
EOF
cat >tests/test_value.c <<'EOF'
#include <smith/test.h>

#include "base/value.h"
#include "tests/helper.h"

void test_value(void) {
    smith_check_equal_i32(base_value(), helper_twenty_one());
}
EOF
cat >tests/helper.h <<'EOF'
#pragma once
int helper_twenty_one(void);
EOF
cat >tests/helper.c <<'EOF'
#include "tests/helper.h"

int helper_twenty_one(void) {
    return 21;
}
EOF
printf 'int lonely_helper(void);\nint lonely_helper(void) {\n    return 1;\n}\n' >tests/lonely.c
cat >build.c <<'EOF'
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_defaults(b, &(SmithDefaults){
                          .flags = smith_list(b, "-I", "src", nullptr),
                          .folders = (const SmithFolder[]){{"src/base", smith_list(b, "-DFROM_BASE=1", nullptr),
                                                                    smith_list(b, "build/gen.h", nullptr)},
                                                                   {}},
                          .link_flags = smith_list(b, "-Wl,-headerpad,0x100", nullptr),
                      });
    smith_step(b, &(SmithStep){.command = smith_list(b, "sh", "-c", "printf '#define GEN_VALUE 7\\n' > build/gen.h", nullptr),
                               .outputs = smith_list(b, "build/gen.h", nullptr)});
    smith_app(b, &(SmithApp){});
    smith_app(b, &(SmithApp){.entry = "tools/tool.c", .name = "tool", .tool = true});
    smith_app_tests(b, &(SmithAppTests){});
    smith_write(b, "info.txt",
                smith_fmt(b, "sources: %s\nobjects: %s\nflags: %s\nlink: %s\nsource flags: %s\n",
                          smith_join(b, smith_sources(b, "src/base"), " "),
                          smith_join(b, smith_objects(b, "release", ""), " "),
                          smith_join(b, smith_profile_flags(b, "fast"), " "),
                          smith_join(b, smith_profile_link_flags(b, "release"), " "),
                          smith_join(b, smith_source_flags(b, "debug", "src/base/value.c"), " ")));
}
EOF
capture smith build
expect "multi: smith build" 0 "$rc"
expect "multi: builds the project's program" yes "$(exists build/debug/multi)"
expect "multi: ... and not the tool" no "$(exists build/debug/tool)"
capture smith run
expect "multi: smith run runs the program, found through -I" 21 "$(last "$out")"
expect "multi: ... after the file its folder's compiles wait for was made" yes "$(exists build/gen.h)"
capture smith build tools
expect "multi: smith build tools builds the tool" yes "$(exists build/debug/tool)"
capture smith build release fast
expect "multi: release and fast build every program" yes "$(exists build/release/tool)"
expect "multi: ... the fast one too" yes "$(exists build/fast/tool)"
capture smith build -t commands build/release/src/base/value.o
expect_contains "multi: release compiles to ThinLTO" "$out" "-flto=thin"
capture smith build -t commands build/release/multi
expect_contains "multi: ... and links with it" "$out" "-flto=thin"
expect_contains "multi: ... and with the defaults' link flags" "$out" "-Wl,-headerpad,0x100"
capture smith build -t commands build/debug/src/base/value.o
expect_contains "multi: a folder's flags reach its sources" "$out" "-DFROM_BASE=1"
capture smith build -t commands build/debug/main.o
case $out in *FROM_BASE*) expect "multi: ... and no others" no yes ;; *) expect "multi: ... and no others" no no ;; esac
expect_contains "multi: build::sources" "$(cat info.txt)" "sources: src/base/value.c"
expect_contains "multi: build::objects" "$(cat info.txt)" "build/release/src/base/value.o"
expect_contains "multi: build::profile_flags: the profile's own" "$(cat info.txt)" "-O2 -DNDEBUG -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function -flto=thin -I src"
expect_contains "multi: build::profile_link_flags" "$(cat info.txt)" "link: -flto=thin -Wl,-headerpad,0x100"
expect_contains "multi: build::source_flags: the folder's" "$(cat info.txt)" "-DFROM_BASE=1"
capture smith test
expect "multi: smith test passes" 0 "$rc"
expect_contains "multi: ... the helper a test includes is linked" "$out" "1 passed"
expect "multi: ... and a helper nothing includes is compiled" yes "$(exists build/debug/tests/lonely.o)"
# Two programs may share a source only with the same flags.
cat >build.c <<'EOF'
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_defaults(b, &(SmithDefaults){.flags = smith_list(b, "-I", "src", nullptr)});
    smith_app(b, &(SmithApp){});
    smith_app(b, &(SmithApp){.entry = "tools/tool.c", .name = "tool", .flags = smith_list(b, "-DX=1", nullptr)});
}
EOF
capture smith build
expect "multi: the same source with different flags: status 2" 2 "$rc"
expect_contains "multi: ... names the source" "$err" "src/base/value.c is compiled twice with different flags"
cat >build.c <<'EOF'
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_app(b, &(SmithApp){});
    smith_defaults(b, &(SmithDefaults){});
}
EOF
capture smith build
expect_contains "multi: smith_defaults after smith_app is refused" "$err" "smith_defaults comes before smith_app"
cd "$work" || exit 1

# 7. Property tests in a .smith file: the key is written c::SmithKey, random::Key or through an alias.
mkdir -p "$work/keys/tests" && cd "$work/keys" || exit 1
cat >main.smith <<'EOF'
module main;
import <stdio.h>;

int main() {
    c::printf("hi\n");
    return 0;
}
EOF
cat >tests/key_test.smith <<'EOF'
module key_test;
import <smith/test.h>;
import smith::random;

export void test_plain(random::Key key) {
    (void)key;
    c::smith_check(true);
}

static i32 helper(i32 x) {
    return x;
}

export void test_helper() {
    c::smith_check_equal_i32(helper(2), 2);
}
EOF
cat >tests/alias_test.smith <<'EOF'
module alias_test;
import <smith/test.h>;
import smith::random as rng;

export void test_aliased(rng::Key key) {
    (void)key;
    c::smith_check(true);
}
EOF
capture smith test
expect "keys: random::Key and an alias's Key are property tests: smith test passes" 0 "$rc"
expect_contains "keys: ... all three tests ran" "$out" "3 passed"
cd "$work" || exit 1

echo
echo "$total conventions checks, $failures failed"
[ "$failures" -eq 0 ]
