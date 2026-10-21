#!/bin/sh
# Tests of <smith/arena.h> and of how a project gets a smith library, with real clang, ninja and clangd. Usage:
# sh tests/arena.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
cc=${CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
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

# 1. The checks, plain and under AddressSanitizer (which lays pushes out on granules).
$cc $flags "$here/tests/arena.c" "$here/lib/arena.c" -o "$work/plain" || exit 1
capture "$work/plain"
expect "library: the checks pass" "0 arena: all checks passed" "$rc $out"
$cc $flags -g -fsanitize=address,undefined "$here/tests/arena.c" "$here/lib/arena.c" -o "$work/asan" || exit 1
capture "$work/asan"
expect "library: the checks pass under ASan and UBSan" "0 arena: all checks passed" "$rc $out"

# 2. The capacity stop is on in every build, NDEBUG included.
$cc $flags -w -DNDEBUG -O2 "$here/tests/arena.c" "$here/lib/arena.c" -o "$work/fast" || exit 1
capture "$work/fast" stop
expect "stop: the status is abort's" 134 "$rc"
expect_contains "stop: the message names the request" "$err" "push of 100 bytes"
expect_contains "stop: the message names the capacity" "$err" "of 128 bytes used"
expect "stop: nothing runs afterwards" "" "$out"

# 3. ASan catches a write just past a push.
capture "$work/asan" oob
expect_contains "asan: a write past a push is caught" "$err" "AddressSanitizer"
expect_contains "asan: it is a poisoned-region error" "$err" "use-after-poison"

# 3b. The wrong pointer type is a compile error with the macro.
cat >"$work/wrong.c" <<'C'
#include <smith/arena.h>

typedef struct { int a; } Foo;
typedef struct { int a; } Bar;

Bar *wrong(SmithArena *arena) {
    Bar *bar = SMITH_ARENA_PUSH(arena, Foo);
    return bar;
}
C
capture $cc $flags -c "$work/wrong.c" -o "$work/wrong.o"
expect "macro: a push of Foo assigned to a Bar pointer fails to compile" yes "$([ "$rc" -ne 0 ] && echo yes)"
expect_contains "macro: ... because of the pointer types" "$err" "incompatible pointer types"

# 4. A project gets the library with smith_library.
mkdir "$work/project" && cd "$work/project" || exit 1
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu23", "-Wall", "-Werror", "-fsanitize=address", "-I", ".smith/include", nullptr);
    SmithObject main = {.compiler = "smith", .source = "main.c", .output = "build/main.o", .flags = flags};
    smith_object(b, &main);
    SmithObject arena = {.compiler = "smith", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
    smith_object(b, &arena);
    SmithProgram program = {
        .linker = "smith",
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
    Item *items = SMITH_ARENA_PUSH_ARRAY(&arena, Item, 4);
    items[3].id = 7;
    printf("item %d, %zu of %zu bytes used\n", items[3].id, arena.used, arena.capacity);
    return 0;
}
C
mkdir "$work/bin" && ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith++"
PATH="$work/bin:$PATH"
capture "$smith" build
expect "project: the build succeeds" 0 "$rc"
expect "project: the header is written" yes "$([ -f .smith/include/smith/arena.h ] && echo yes)"
expect "project: the source is written" yes "$([ -f .smith/lib/arena.c ] && echo yes)"
capture build/app
expect_contains "project: the program runs" "$out" "item 7"
expect "project: the program exits 0" 0 "$rc"
before=$(stat -f %m .smith/lib/arena.c 2>/dev/null || stat -c %Y .smith/lib/arena.c)
sleep 1
capture "$smith" build
after=$(stat -f %m .smith/lib/arena.c 2>/dev/null || stat -c %Y .smith/lib/arena.c)
expect "project: an unchanged library is not rewritten" "$before" "$after"
entry=$(grep -A12 '"file": "main.c"' compile_commands.json | tr -d '\n')
expect_contains "project: compile_commands.json gives main.c the include flag" "$entry" '"-I"'
expect_contains "project: ... which is .smith/include" "$entry" '".smith/include"'
if [ -x /usr/bin/clangd ]; then
    capture /usr/bin/clangd --check=main.c
    errors=$(printf '%s\n%s\n' "$out" "$err" | grep -c 'E\[')
    expect "project: clangd reports no errors in main.c" 0 "$errors"
fi

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
expect_contains "unknown: the message names the library" "$err" 'smith: build.c: smith_library: unknown library "nope"; known: arena, io, random, test'

echo
echo "$total arena checks, $failures failed"
[ "$failures" -eq 0 ]
