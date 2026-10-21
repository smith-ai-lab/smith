#!/bin/sh
# The corpus of the .smith language (tests/smith-lang/cases), run through the smith given, which must have clang inside
# with the token hook (a stage0 has no compiler to lower for). Usage:
#   sh tests/smith-lang.sh path/to/smith [case ...]       run the corpus, or only the named cases
#   UPDATE=1 sh tests/smith-lang.sh path/to/smith         write what each case produced as its expectation (read the diff)
#
# A case is a folder:
#   src/             the sources; module a.b is src/a/b.smith; src/main.smith, if there, is linked with the other
#                    .smith files and any .c or .cpp beside them, and run
#   expect/stdout    what the program prints
#   expect/error     the errors the build must report, as `file:line:column: message`, one per line (the build must fail)
#   expect/lowered/  the C each file is lowered to (a.b.c), by `smith --smith-lower`: the lowering of the text as written,
#                    with no macros and no parser (the build's own run is checked by the program, the symbols and the headers)
#   expect/header/   the generated header of each module (a.b.h)
#   expect/symbols   the symbols the objects define (nm -g), sorted
#   flags            extra compiler flags, if any
# Everything runs in the case's own folder, so paths in messages are relative to it.
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
shift
here=$(cd "$(dirname "$0")" && pwd)
corpus=$here/smith-lang/cases
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
failures=0
total=0

if ! "$smith" --smith-which -x c 2>/dev/null | grep -q "clang in-process"; then
    echo "skip  smith-lang: this smith has no clang inside"
    exit 0
fi

fail() {
    failures=$((failures + 1))
    echo "FAIL  $1"
}

check() { # name, expected file, actual file
    total=$((total + 1))
    if [ "${UPDATE:-}" = 1 ]; then
        mkdir -p "$(dirname "$2")"
        cp "$3" "$2"
        echo "wrote $2"
    elif [ ! -f "$2" ]; then
        fail "$1 (there is no $2)"
    elif cmp -s "$2" "$3"; then
        echo "ok    $1"
    else
        fail "$1"
        diff "$2" "$3" | head -20
    fi
}

# Every compile of the case, into $out. Sets $objects and $status.
build_case() { # out
    out=$1
    flags="-std=gnu23 -g0 -fno-color-diagnostics -Wno-unused-function -I $out/include"
    [ -f flags ] && flags="$flags $(cat flags)"
    status=0
    objects=""
    sources=$(cd src && find . -name '*.smith' | sed 's|^\./||' | LC_ALL=C sort)
    for file in $sources; do # the headers first, as the build will: no compile waits on another
        mkdir -p "$out/include/$(dirname "$file")"
        "$smith" --smith-header "src/$file" -o "$out/include/${file%.smith}.h" 2>>"$out/stderr"
        module=$(echo "${file%.smith}" | tr / .)
        [ -f "$out/include/${file%.smith}.h" ] && cp "$out/include/${file%.smith}.h" "$out/header/$module.h"
    done
    for file in $sources; do
        module=$(echo "${file%.smith}" | tr / .)
        "$smith" $flags -c "src/$file" -o "$out/obj/$module.o" 2>>"$out/stderr" || status=1
        "$smith" --smith-lower "src/$file" >"$out/lowered/$module.c" 2>/dev/null
        objects="$objects $out/obj/$module.o"
    done
    for file in $(cd src && find . -name '*.c' -o -name '*.cpp' | sed 's|^\./||' | LC_ALL=C sort); do
        object="$out/obj/$(echo "$file" | tr / .).o"
        "$smith" $flags -c "src/$file" -o "$object" 2>>"$out/stderr" || status=1
        objects="$objects $object"
    done
    : >"$out/stdout"
    if [ $status = 0 ] && [ -f src/main.smith ]; then
        if "$smith" $objects -o "$out/program" 2>>"$out/stderr"; then
            "$out/program" >"$out/stdout" 2>>"$out/stderr" || echo "(exit $?)" >>"$out/stdout"
        else
            status=1
        fi
    fi
}

run_case() { # name
    name=$1
    out=$work/$name
    mkdir -p "$out/include" "$out/lowered" "$out/header" "$out/obj"
    : >"$out/stderr"
    cd "$corpus/$name" || return
    build_case "$out"
    # The errors: file:line:column: message, and nothing else (no warnings, notes, source lines or counts).
    sed -n 's/^\([^ :]*:[0-9]*:[0-9]*\): error: \(.*\)$/\1: \2/p' "$out/stderr" | LC_ALL=C sort -u >"$out/error"
    wants_error=false
    [ -f expect/error ] && wants_error=true
    [ "${UPDATE:-}" = 1 ] && [ -s "$out/error" ] && wants_error=true
    if $wants_error; then
        check "$name: the errors" expect/error "$out/error"
        total=$((total + 1))
        if [ $status = 0 ]; then fail "$name: the build should have failed"; fi
    elif [ $status != 0 ] || [ -s "$out/error" ]; then
        total=$((total + 1))
        fail "$name: the build failed"
        head -20 "$out/stderr"
    else
        total=$((total + 1))
        echo "ok    $name: it builds"
    fi
    if [ -f expect/stdout ] || { [ "${UPDATE:-}" = 1 ] && [ -s "$out/stdout" ]; }; then
        check "$name: the output" expect/stdout "$out/stdout"
    fi
    if [ "${UPDATE:-}" = 1 ]; then # the files a successful build made are the expectation
        [ $status = 0 ] && for actual in "$out"/lowered/*.c "$out"/header/*.h; do
            [ -s "$actual" ] || continue
            kind=$(basename "$(dirname "$actual")")
            check "$name: $kind $(basename "$actual")" "expect/$kind/$(basename "$actual")" "$actual"
        done
    else
        for expected in expect/lowered/* expect/header/*; do
            [ -f "$expected" ] || continue
            kind=$(basename "$(dirname "$expected")")
            check "$name: $kind $(basename "$expected")" "$expected" "$out/$kind/$(basename "$expected")"
        done
    fi
    if [ -f expect/symbols ]; then
        for object in $objects; do nm -g "$object" 2>/dev/null; done | grep -v ' U ' | awk '{print $2, $3}' |
            LC_ALL=C sort >"$out/symbols"
        check "$name: symbols" expect/symbols "$out/symbols"
    fi
}

if [ $# -gt 0 ]; then
    names="$*"
else
    names=$(ls "$corpus")
fi
for name in $names; do
    run_case "$name"
done
echo "smith-lang: $((total - failures)) of $total passed"
[ $failures = 0 ]
