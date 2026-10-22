#!/bin/sh
# Plain C in a .smith file means exactly what it means in a .c file: smith's own C sources, renamed to .smith, compile to
# the same code. Usage: sh tests/smith-lang-c.sh path/to/smith
# Each source is compiled twice with the same flags, once as x.c and once as x.smith (in a copy of the tree, so that its
# #includes find what they found), and the two results must be identical. They are compared as assembly (-S), which is
# stricter than objects and shows where they differ; the one thing that may differ is the file's own name, where a
# program prints it (its extension): __FILE__ is set to one name, and ".smith" at the end of a name in a string is read
# as ".c" on both sides. The test needs a smith with clang inside; a stage0
# skips it.
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
    cp "$2" "$smith_file"
    # shellcheck disable=SC2086
    "$smith" cc $flags $3 -S "$2" -o "$work/plain/$1.s" 2>"$work/plain/$1.err"
    plain=$?
    # shellcheck disable=SC2086
    "$smith" cc $flags $3 -S "$smith_file" -o "$work/lowered/$1.s" 2>"$work/lowered/$1.err"
    lowered=$?
    rm -f "$smith_file"
    # Both sides read ".smith" at the end of a name in a string as ".c" (the source's own strings that say ".smith" too).
    for side in plain lowered; do
        sed 's/\([A-Za-z0-9_]\)\.smith"/\1.c"/g' "$work/$side/$1.s" >"$work/$side/$1.n" 2>/dev/null
    done
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

echo "smith-lang-c: $((total - failures)) of $total identical"
[ $failures = 0 ]
