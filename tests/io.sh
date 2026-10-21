#!/bin/sh
# Tests of <smith/io.h>, with real clang, ninja and clangd. Usage: sh tests/io.sh path/to/smith
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

warn="-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wimplicit-fallthrough -Wmissing-prototypes -Wstrict-prototypes -Wvla -Werror"
sources="$here/tests/io.c $here/lib/io.c $here/lib/arena.c $here/lib/random.c"
mkdir "$work/run" "$work/bin"
cd "$work/run" || exit 1

# 1. The checks, under AddressSanitizer and UBSan at -O0, and at -O2 (where the memory check also runs). Nothing is
# printed behind the caller's back: stderr stays empty.
$cc -std=gnu23 $warn -I "$here/include" -O0 -g -fsanitize=address,undefined $sources -o "$work/bin/asan" || exit 1
capture "$work/bin/asan"
expect "checks: pass at -O0 under ASan and UBSan" "0 io: all checks passed" "$rc $out"
expect "checks: ... and nothing was printed to stderr" "" "$err"
$cc -std=gnu23 $warn -I "$here/include" -O2 $sources -o "$work/bin/fast" || exit 1
capture "$work/bin/fast"
expect "checks: pass at -O2" "0 io: all checks passed" "$rc $out"
expect "checks: ... and nothing was printed to stderr" "" "$err"

# 2. The library compiles as -std=gnu23 and -std=gnu2y with the warnings of smith new.
for std in gnu23 gnu2y; do
    capture $cc -std=$std $warn -I "$here/include" -c "$here/lib/io.c" -o "$work/io.o"
    expect "library: compiles as -std=$std with the new project's warnings" 0 "$rc"
done
game="-Wdouble-promotion -Wundef -Wpointer-arith -Wcast-qual -Wformat=2 -Wmissing-variable-declarations -Wcomma -Wunreachable-code"
for std in gnu23 gnu2y; do
    capture $cc -std=$std $warn $game -I "$here/include" -c "$here/lib/io.c" -o "$work/io.o"
    expect "library: compiles as -std=$std under the extra warnings game and learner add" 0 "$rc"
done
capture $cc -std=gnu23 $warn -O2 -DNDEBUG -I "$here/include" -c "$here/lib/io.c" -o "$work/io.o"
expect "library: compiles with NDEBUG and the same warnings" 0 "$rc"

# 3. The arena is a reservation: making the io commits nothing, touching memory commits what is touched.
capture "$work/bin/fast" memory
expect "memory: reserves 64 GiB" 0 "$rc"
expect_contains "memory: ... which is address space only" "$out" "reserved 65536 MiB"
expect_contains "memory: ... touched memory is resident" "$out" "+256 MiB after touching 256 MiB"
capture "$work/bin/asan" memory
expect "memory: the same under AddressSanitizer, which poisons lazily" 0 "$rc"
expect_contains "memory: ... and the reservation is the same 64 GiB" "$out" "reserved 65536 MiB"

# 4. SMITH_SEED replays a run; without it the key is new each time.
capture env SMITH_SEED=42 "$work/bin/fast" seed
first=$out
capture env SMITH_SEED=42 "$work/bin/fast" seed
expect "seed: the same SMITH_SEED gives the same key" "$first" "$out"
expect_contains "seed: ... and the seed is the one given" "$first" "42 "
capture env SMITH_SEED=43 "$work/bin/fast" seed
expect "seed: another SMITH_SEED gives another key" yes "$([ "$out" != "$first" ] && echo yes)"
capture env -u SMITH_SEED "$work/bin/fast" seed
first=$out
capture env -u SMITH_SEED "$work/bin/fast" seed
expect "seed: without SMITH_SEED each run has its own key" yes "$([ "$out" != "$first" ] && echo yes)"
capture env SMITH_SEED=banana "$work/bin/fast" seed
expect "seed: a SMITH_SEED that is no number stops the program" 134 "$rc"
expect_contains "seed: ... saying so" "$err" "SMITH_SEED is not a number"

# 5. The console reads lines from a pipe to the end of input, with or without a final newline.
capture sh -c "printf 'a\nb\nlast' | '$work/bin/fast' lines"
expect "console: a pipe ending without a newline" "0 3 lines, last=last" "$rc $out"
capture sh -c "printf 'a\nb\n' | '$work/bin/fast' lines"
expect "console: a pipe ending with a newline" "0 2 lines, last=b" "$rc $out"
capture sh -c "'$work/bin/fast' lines </dev/null"
expect "console: an empty input ends at once" "0 0 lines, last=" "$rc $out"
capture sh -c "head -c 100000 /dev/zero | tr '\\0' x | '$work/bin/fast' lines"
expect "console: a line longer than the buffer" "0 1 lines, last=$(head -c 100000 /dev/zero | tr '\0' x)" "$rc $out"

# 5b. The same loop ends when stdin is a regular file, which kqueue never reports readable at its end, however it is
# fed. The lines program stops itself with SIGALRM after 10 s, so a hang is a failure (status 142).
printf 'a\nb\nlast' >"$work/lines.txt"
: >"$work/empty.txt"
capture sh -c "'$work/bin/fast' lines <'$work/lines.txt'"
expect "console: a regular file with lines ends" "0 3 lines, last=last" "$rc $out"
capture sh -c "'$work/bin/asan' lines <'$work/lines.txt'"
expect "console: ... under ASan too" "0 3 lines, last=last" "$rc $out"
capture sh -c "'$work/bin/fast' lines <'$work/empty.txt'"
expect "console: an empty regular file ends" "0 0 lines, last=" "$rc $out"
capture sh -c "'$work/bin/fast' lines </dev/null"
expect "console: /dev/null ends" "0 0 lines, last=" "$rc $out"
capture sh -c "printf 'a\nb\nlast' | '$work/bin/fast' lines"
expect "console: a pipe ends" "0 3 lines, last=last" "$rc $out"
capture sh -c "(printf 'a\nb\n'; exit 0) | '$work/bin/fast' lines"
expect "console: a pipe closed early ends with the lines it had" "0 2 lines, last=b" "$rc $out"

# 6. A project gets io with smith_library, along with the arena and random it uses, and one line makes everything.
mkdir "$work/project" && cd "$work/project" || exit 1
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *flags = smith_list(b, "-std=gnu23", "-Wall", "-Werror", "-fsanitize=address", "-I", ".smith/include", nullptr);
    SmithObject main = {.compiler = "smith", .source = "main.c", .output = "build/main.o", .flags = flags};
    smith_object(b, &main);
    SmithObject io = {.compiler = "smith", .source = smith_library(b, "io"), .output = "build/smith/io.o", .flags = flags};
    smith_object(b, &io);
    SmithObject random = {.compiler = "smith", .source = smith_library(b, "random"), .output = "build/smith/random.o", .flags = flags};
    smith_object(b, &random);
    SmithObject arena = {.compiler = "smith", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
    smith_object(b, &arena);
    SmithProgram program = {
        .linker = "smith",
        .output = "build/app",
        .flags = smith_list(b, "-fsanitize=address", nullptr),
        .objects = (const SmithObject *[]){&main, &io, &random, &arena, nullptr},
    };
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
    smith_compile_commands(b, "compile_commands.json");
}
C
cat >main.c <<'C'
#include <smith/io.h>

#include <stdio.h>

// Takes the two capabilities it uses and nothing else.
static size_t count_words(SmithFiles *files, SmithArena *arena, const char *path) {
    SmithRead text = smith_files_read(files, arena, path);
    size_t words = 0;
    bool inside = false;
    for (size_t i = 0; i < text.size; i++) {
        bool space = text.data[i] == ' ' || text.data[i] == '\n';
        if (!space && !inside) words += 1;
        inside = !space;
    }
    return words;
}

int main(int argc, char **argv) {
    SmithIo io = smith_io_make(argc, argv);
    SmithError error = smith_files_write(&io.files, "words.txt", "one two  three\nfour", 19);
    printf("write %d, words %zu, seed %d\n", error.code, count_words(&io.files, &io.arena, "words.txt"), io.seed != 0);
    return 0;
}
C
mkdir "$work/bin2" && ln -s "$smith" "$work/bin2/smith" && ln -s "$smith" "$work/bin2/smith++"
PATH="$work/bin2:$PATH"
capture "$smith" build
expect "project: the build succeeds" 0 "$rc"
expect "project: the header is written" yes "$([ -f .smith/include/smith/io.h ] && echo yes)"
expect "project: the source is written" yes "$([ -f .smith/lib/io.c ] && echo yes)"
capture build/app
expect "project: the program runs" "0 write 0, words 4, seed 1" "$rc $out"
if [ -x /opt/homebrew/opt/llvm/bin/clangd ]; then
    capture /opt/homebrew/opt/llvm/bin/clangd --check=main.c
    errors=$(printf '%s\n%s\n' "$out" "$err" | grep -c 'E\[')
    expect "project: clangd reports no errors in main.c" 0 "$errors"
fi

echo
echo "$total io checks, $failures failed"
[ "$failures" -eq 0 ]
