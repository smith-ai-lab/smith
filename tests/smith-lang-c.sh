#!/bin/sh
# Plain C in a .smith file means exactly what it means in a .c file: smith's own C sources, renamed to .smith, compile to
# the same code. Usage: sh tests/smith-lang-c.sh path/to/smith
# Each source is compiled twice with the same flags, once as x.c and once as x.smith (in a copy of the tree, so that its
# #includes find what they found), and the two results must be identical. They are compared as assembly (-S), which is
# stricter than objects and shows where they differ; the one thing that may differ is the file's own name, where a
# program prints it (its extension): __FILE__ is set to one name, and the file's own name with ".smith" in a string is
# read as ".c" (only its own: other strings that end in ".smith" are code, and must match). Then each program in
# tests/smith-lang-c/ (array parameters and sizeof of one, a [*] prototype, the language's words as names, ordinary
# constructs, text arrays and `?:` in brackets, which run-time arrays must leave alone) is built and run as x.c and as x.smith, and both must print what x.out holds. The test needs a smith with clang inside; a stage0 skips it.
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
failures=0
total=0

if ! "$smith" cc --smith-which -x c 2>/dev/null | grep -q "token-hook yes"; then
    echo "skip  smith-lang-c: this smith has no clang with the token hook inside"
    exit 0
fi

mkdir -p "$work/tree"
for item in smith.c build.c bootstrap include lib runtime scripts templates tests shim; do
    cp -R "$here/$item" "$work/tree/$item"
done
cd "$work/tree" || exit 1

flags="-std=gnu23 -O1 -g0 -Wno-builtin-macro-redefined -D__FILE__=\"unit\" -I include"

compare() { # name, source, extra flags
    total=$((total + 1))
    mkdir -p "$work/plain" "$work/lowered"
    # The renamed copy sits where the original does, so that #include "..." finds its neighbours.
    smith_file="${2%.c}.smith"
    base=$(basename "${2%.c}")
    own="s|\([^A-Za-z0-9_/.-]\)$base\.smith\"|\1$base.c\"|g"
    cp "$2" "$smith_file"
    # shellcheck disable=SC2086
    "$smith" cc $flags $3 -S "$2" -o "$work/plain/$1.s" 2>"$work/plain/$1.err"
    plain=$?
    # shellcheck disable=SC2086
    "$smith" cc $flags $3 -S "$smith_file" -o "$work/lowered/$1.s" 2>"$work/lowered/$1.err"
    lowered=$?
    rm -f "$smith_file"
    # The file's own name in a string is read as ".c" on the lowered side; any other ".smith" in a string is code.
    cp "$work/plain/$1.s" "$work/plain/$1.n" 2>/dev/null
    sed "$own" "$work/lowered/$1.s" >"$work/lowered/$1.n" 2>/dev/null
    if [ $plain != $lowered ]; then
        failures=$((failures + 1))
        echo "FAIL  $1: exit $plain as .c, $lowered as .smith"
        sed 's/smith:/smith/' "$work/lowered/$1.err" | head -5
    elif [ $plain = 0 ] && ! cmp -s "$work/plain/$1.n" "$work/lowered/$1.n"; then
        failures=$((failures + 1))
        echo "FAIL  $1: the code differs"
        diff "$work/plain/$1.n" "$work/lowered/$1.n" | head -10
    elif [ "$(sed 's/\.c\([: ]\)/.smith\1/g' "$work/plain/$1.err")" != "$(cat "$work/lowered/$1.err")" ]; then
        failures=$((failures + 1))
        echo "FAIL  $1: the messages differ"
    else
        echo "ok    $1"
    fi
}

in_process="-DSMITH_IN_PROCESS -DSMITH_CLANG_RESOURCE=\"tests/stub.c\" -DSMITH_CLANG_ID=\"llvm-0 0\""
compare smith smith.c "$in_process"
compare build build.c ""
compare build_runtime runtime/build_runtime.c ""
compare stub tests/stub.c "-DNAME=\"stub\""
compare pack scripts/pack.c ""
compare lib_http lib/http.c ""
# Not tests/random.c: it includes the lowering of random, and both print the name "random.c", which the compiler makes one string;
# as random.smith it is two, so the constants after it are numbered differently. (Nothing else differs.)
for name in arena io http untar; do
    compare "test_$name" "tests/$name.c" ""
done
compare templates_main templates/new/main.c "-std=gnu2y"
# Where C does not require a constant, the evaluator that runs C functions for .smith (patches/clang-constexpr-c.patch) does nothing.
compare folding tests/smith-lang-c/folding.c ""
compare folding_o2 tests/smith-lang-c/folding.c "-O2 -Wall -Wpedantic -Wvla -Wgnu-folding-constant"
compare folding_c17 tests/smith-lang-c/folding.c "-std=c17"

# A program that prints: the same output as .c and as .smith, and the one in its .out file.
behave() { # name
    total=$((total + 1))
    src="tests/smith-lang-c/$1"
    mkdir -p "$work/run"
    cp "$src.c" "$src.smith"
    status=ok
    # shellcheck disable=SC2086
    "$smith" cc $flags -w "$src.c" -o "$work/run/$1.c.bin" 2>"$work/run/$1.c.err" || status="the .c does not build"
    # shellcheck disable=SC2086
    "$smith" cc $flags -w "$src.smith" -o "$work/run/$1.smith.bin" 2>"$work/run/$1.smith.err" || status="the .smith does not build"
    rm -f "$src.smith"
    if [ "$status" = ok ]; then
        "$work/run/$1.c.bin" >"$work/run/$1.c.out" 2>&1
        "$work/run/$1.smith.bin" >"$work/run/$1.smith.out" 2>&1
        cmp -s "$work/run/$1.c.out" "$src.out" || status="the .c prints other than $1.out"
        cmp -s "$work/run/$1.smith.out" "$src.out" || status="the .smith prints other than $1.out"
    fi
    if [ "$status" = ok ]; then
        echo "ok    run $1"
    else
        failures=$((failures + 1))
        echo "FAIL  run $1: $status"
        head -5 "$work/run/$1.smith.err"
        diff "$src.out" "$work/run/$1.smith.out" | head -5
    fi
}

for name in arrays names ordinary sized_names slices; do
    behave "$name"
done

# Where C requires a constant, a .smith file runs the function; a .c file reports the error it always did; the target is the
# target's own (a `long` of a Windows target is 4 bytes).
constexpr_c() { # name, expected exit of the .c, expected exit of the .smith, extra flags
    total=$((total + 1))
    mkdir -p "$work/constexpr"
    printf 'static unsigned long width(void) { return sizeof(long); }\nstatic int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }\nconstexpr int x = fib(10);\nstatic_assert(x == 55);\nstatic_assert(width() == %s);\n' "$4" >"$work/constexpr/$1.c"
    cp "$work/constexpr/$1.c" "$work/constexpr/$1.smith"
    # shellcheck disable=SC2086
    "$smith" cc -std=gnu23 -fsyntax-only $5 "$work/constexpr/$1.c" 2>/dev/null
    plain=$?
    # shellcheck disable=SC2086
    "$smith" cc -std=gnu23 -fsyntax-only $5 "$work/constexpr/$1.smith" 2>"$work/constexpr/$1.err"
    lowered=$?
    if [ $plain = 0 ] || [ $lowered != 0 ]; then
        failures=$((failures + 1))
        echo "FAIL  $1: exit $plain as .c (an error), $lowered as .smith (0)"
        head -5 "$work/constexpr/$1.err"
    else
        echo "ok    $1"
    fi
}
constexpr_c constexpr_host 1 0 8 ""
constexpr_c constexpr_windows 1 0 4 "--target=x86_64-pc-windows-msvc -nostdinc"

# A constexpr pointer into a string literal or a constexpr object (C23 requires null) is a value in a .smith file; a .c file
# still reports "constexpr pointer initializer is not null".
constexpr_pointer() { # name
    total=$((total + 1))
    mkdir -p "$work/constexpr"
    printf 'constexpr char hello[] = "hello";\nconstexpr const char *word = "abc";\nconstexpr const char *inside = &hello[1];\nstatic_assert(word[1] == %sb%s);\nstatic_assert(inside[0] == %se%s);\n' "'" "'" "'" "'" >"$work/constexpr/$1.c"
    cp "$work/constexpr/$1.c" "$work/constexpr/$1.smith"
    "$smith" cc -std=gnu23 -fsyntax-only "$work/constexpr/$1.c" 2>"$work/constexpr/$1.err"
    plain=$?
    "$smith" cc -std=gnu23 -fsyntax-only "$work/constexpr/$1.smith" 2>/dev/null
    lowered=$?
    if [ $plain = 0 ] || [ $lowered != 0 ] || ! grep -q "constexpr pointer initializer is not null" "$work/constexpr/$1.err"; then
        failures=$((failures + 1))
        echo "FAIL  $1: exit $plain as .c (not null), $lowered as .smith (0)"
        head -5 "$work/constexpr/$1.err"
    else
        echo "ok    $1"
    fi
}
constexpr_pointer constexpr_pointer

echo "smith-lang-c: $((total - failures)) of $total identical"
[ $failures = 0 ]
