#!/bin/sh
# Tests of <smith/arena.h> and of how a project gets a smith library, with real clang, ninja and clangd. Usage:
# sh tests/arena.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
cc=${CC:-cc}
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

flags="-std=gnu23 -Wall -Wextra -Wpedantic -Werror -I $here/include"

# The module itself is compiled by a smith whose clang has the token hook; a stage0 skips the cases that need it.
hook=yes
if ! "$smith" cc --smith-which -x c 2>/dev/null | grep -q "token-hook yes"; then hook=no; fi
module="$here/lib/smith/arena.smith"

# 1. The checks, plain and under AddressSanitizer (which lays pushes out on granules).
$cc $flags "$here/tests/arena.c" "$here/bootstrap/lib/smith/arena.c" -o "$work/plain" || exit 1
capture "$work/plain"
expect "library: the checks pass" "0 arena: all checks passed" "$rc $out"
$cc $flags -g -fsanitize=address,undefined "$here/tests/arena.c" "$here/bootstrap/lib/smith/arena.c" -o "$work/asan" || exit 1
capture "$work/asan"
expect "library: the checks pass under ASan and UBSan" "0 arena: all checks passed" "$rc $out"

# 2. The capacity stop is on in every build, NDEBUG included.
$cc $flags -w -DNDEBUG -O2 "$here/tests/arena.c" "$here/bootstrap/lib/smith/arena.c" -o "$work/fast" || exit 1
capture "$work/fast" stop
expect "stop: the status is abort's" 134 "$rc"
expect_contains "stop: the message names the request" "$err" "push of 100 bytes"
expect_contains "stop: the message names the capacity" "$err" "of 128 bytes used"
expect "stop: nothing runs afterwards" "" "$out"

# 3. ASan catches a write just past a push.
capture "$work/asan" oob
expect_contains "asan: a write past a push is caught" "$err" "AddressSanitizer"
expect_contains "asan: it is a poisoned-region error" "$err" "use-after-poison"

# 3a. The poison is lazy: a 64 GiB arena under ASan has a small resident footprint after make and a push, an overflow
# just past the push (or within the 64 KiB window past it) is still caught, and a push read after a reset is caught.
capture "$work/asan" big
expect "lazy: a 64 GiB arena under ASan makes little memory resident" 0 "$rc"
expect_contains "lazy: ... the growth is printed" "$out" "resident growth"
capture "$work/plain" big
expect "lazy: the same without ASan" 0 "$rc"
capture "$work/asan" bigoob
expect_contains "lazy: a write one byte past a push in a 64 GiB arena is caught" "$err" "AddressSanitizer"
capture "$work/asan" bigfar
expect_contains "lazy: a write 60,000 bytes past a push is caught (inside the window)" "$err" "AddressSanitizer"
capture "$work/asan" stale
expect_contains "lazy: a read of a push after a reset is caught" "$err" "use-after-poison"

# 3b. A carved arena is a sub-arena of a reservation: it commits nothing until used, and under ASan it poisons lazily
# for itself, so it catches what any arena catches.
capture "$work/asan" carvelazy
expect "carve: 2 GiB carved out of a 64 GiB arena under ASan makes little memory resident" 0 "$rc"
expect_contains "carve: ... the growth is printed" "$out" "resident growth"
capture "$work/plain" carvelazy
expect "carve: the same without ASan" 0 "$rc"
capture "$work/asan" carveoob
expect_contains "carve: a write one byte past a push in a carved arena is caught" "$err" "AddressSanitizer"
capture "$work/asan" carvefar
expect_contains "carve: a write 60,000 bytes past a push is caught (inside the window)" "$err" "AddressSanitizer"
capture "$work/asan" carvestale
expect_contains "carve: a read of a push after a reset is caught" "$err" "use-after-poison"
capture "$work/fast" carvestop
expect "carve: past the parent's capacity the status is abort's" 134 "$rc"
expect_contains "carve: ... and the message names the request" "$err" "push of 2048 bytes"

# 3c. The module compiles as C23 and C2y with the warnings of smith new.
for std in gnu23 gnu2y; do
    [ $hook = yes ] || break
    capture "$smith" cc -std=$std -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -I "$here/include" -iquote "$here/include/smith" -c "$module" -o "$work/a.o"
    expect "library: compiles as -std=$std with the new project's warnings" 0 "$rc"
done

if [ $hook = no ]; then
    echo "skip  project: this smith has no clang with the token hook inside"
    echo
    echo "$total arena checks, $failures failed"
    [ "$failures" -eq 0 ]
    exit
fi

# 4. A project gets the library with smith_library.
mkdir "$work/project" && cd "$work/project" || exit 1
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu23", "-Wall", "-Werror", "-fsanitize=address", "-I", ".smith/include", nullptr);
    SmithObject main = {.compiler = "smith-cc", .source = "main.c", .output = "build/main.o", .flags = flags};
    smith_object(b, &main);
    SmithObject arena = {.compiler = "smith-cc", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
    smith_object(b, &arena);
    SmithProgram program = {
        .linker = "smith-cc",
        .output = "build/app",
        .flags = smith_list(b, "-fsanitize=address", nullptr),
        .objects = (const SmithObject *[]){&main, &arena, nullptr},
    };
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
    smith_compile_commands(b, "compile_commands.json");
}
C
cat >main.c <<'C'
#include <smith/arena.h>

#include <stdio.h>

typedef struct {
    int id;
    double weight;
} Item;

int main(void) {
    alignas(smith_arena_align_max) static unsigned char memory[1024];
    SmithArena arena = smith_arena_make(memory, sizeof memory);
    Item *items = smith_arena_push_array(&arena, sizeof(Item), 4, alignof(Item));
    items[3].id = 7;
    printf("item %d, %zu of %zu bytes used\n", items[3].id, arena.used, arena.capacity);
    return 0;
}
C
mkdir "$work/bin" && ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" && ln -s "$smith" "$work/bin/smith-c++"
PATH="$work/bin:$PATH"
capture "$smith" build
expect "project: the build succeeds" 0 "$rc"
expect "project: the header is written" yes "$([ -f .smith/include/smith/arena.h ] && echo yes)"
expect "project: the source is written" yes "$([ -f .smith/lib/smith/arena.smith ] && echo yes)"
capture build/app
expect_contains "project: the program runs" "$out" "item 7"
expect "project: the program exits 0" 0 "$rc"
before=$(stat -f %m .smith/lib/smith/arena.smith 2>/dev/null || stat -c %Y .smith/lib/smith/arena.smith)
sleep 1
capture "$smith" build
after=$(stat -f %m .smith/lib/smith/arena.smith 2>/dev/null || stat -c %Y .smith/lib/smith/arena.smith)
expect "project: an unchanged library is not rewritten" "$before" "$after"
entry=$(grep -A12 '"file": "main.c"' compile_commands.json | tr -d '\n')
expect_contains "project: compile_commands.json gives main.c the include flag" "$entry" '"-I"'
expect_contains "project: ... which is .smith/include" "$entry" '".smith/include"'
if "$smith" lsp --version >/dev/null 2>&1; then
    capture "$smith" lsp --check=main.c
    errors=$(printf '%s\n%s\n' "$out" "$err" | grep -c 'E\[')
    expect "project: clangd reports no errors in main.c" 0 "$errors"
fi

# 4b. A .smith file uses the library with types as arguments: arena::push(&a, Point) and arena::push_array(&a, i32, n),
# beside the C file of 4 (one header, one object). A count that overflows is refused with the C message.
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu23", "-Wall", "-Werror", "-fsanitize=address", "-I", ".smith/include", nullptr);
    SmithObject typed = {.compiler = "smith-cc", .source = "typed.smith", .output = "build/typed.o", .flags = flags};
    smith_object(b, &typed);
    SmithObject arena = {.compiler = "smith-cc", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
    smith_object(b, &arena);
    SmithProgram program = {
        .linker = "smith-cc",
        .output = "build/typed",
        .flags = smith_list(b, "-fsanitize=address", nullptr),
        .objects = (const SmithObject *[]){&typed, &arena, nullptr},
    };
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
}
C
cat >typed.smith <<'S'
import <stdio.h>;
import <stdlib.h>;
import smith::arena;

typedef struct {
    i32 id;
    f64 weight;
} Point;

i32 main(i32 argc, char **argv) {
    alignas(arena::align_max) static u8 memory[4096];
    arena::Arena a = arena::make(memory, sizeof memory);
    Point *point = arena::push(&a, Point);
    point->id = 3;
    Point *points = arena::push_array(&a, Point, 4);
    points[3].id = 9;
    i32 *ints = arena::push_array(&a, i32, 10);
    ints[9] = 5;
    c::printf("%d %d %d zeroed %d, %zu used\n", point->id, points[3].id, ints[9], (i32)(points[0].weight == 0 && ints[0] == 0),
              a.used);
    if (argc > 1) {
        usize count = c::strtoull(argv[1], nullptr, 10);
        Point *too_many = arena::push_array(&a, Point, count);
        c::printf("not reached %p\n", (void *)too_many);
    }
    return 0;
}
S
capture "$smith" build
expect "typed: a .smith file using arena::push and arena::push_array builds" 0 "$rc"
capture build/typed
expect "typed: push and push_array give zeroed, usable memory" "0 3 9 5 zeroed 1" "$rc $(echo "$out" | cut -d, -f1)"
capture build/typed 18446744073709551615
expect "typed: a count that overflows size_t is refused" 134 "$rc"
expect_contains "typed: ... with the C message" "$err" "items of 16 bytes overflow size_t"
capture build/typed 1000000
expect "typed: a count past the capacity is refused" 134 "$rc"
expect_contains "typed: ... naming the request" "$err" "push of 16000000 bytes (align 8)"

# 5. An unknown library is an error that lists the known names.
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep step = {.command = (const char *[]){"echo", smith_library(b, "nope"), nullptr}};
    smith_task(b, "x", &step);
}
C
capture "$smith" build
expect "unknown: the build fails with status 2" 2 "$rc"
expect_contains "unknown: the message names the library" "$err" 'smith: build.c: smith_library: unknown library "nope"; known: contract, io, json, test, trace, arena, random, text'

echo
echo "$total arena checks, $failures failed"
[ "$failures" -eq 0 ]
