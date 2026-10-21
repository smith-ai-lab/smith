#!/bin/sh
# End-to-end tests for the files a smith with clang inside carries and writes on first use: clang's builtin headers, the
# compiler-rt runtimes, libLTO.dylib, and the bin/clang the driver runs again for -cc1. Every case uses a data folder and
# a cache of its own (XDG_DATA_HOME, XDG_CACHE_HOME), so the real ones are never read or changed. A smith without clang
# inside (the stage0) has none of this, and the tests are skipped.
# Usage: sh tests/resource.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
export XDG_DATA_HOME=$work/data
export XDG_CACHE_HOME=$work/cache

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

exists() { [ -e "$1" ] && echo yes || echo no; }

if [ "$("$smith" --smith-which -c a.c | head -n 1 | cut -d ' ' -f 1,2)" != "clang in-process" ]; then
    echo "skipped: this smith has no clang inside (the stage0)"
    exit 0
fi

mkdir "$work/src"
cd "$work/src" || exit 1
printf '#include <stddef.h>\n#include <stdio.h>\nint main(void) { puts("hi"); return 0; }\n' >hello.c
clang_folders=$XDG_DATA_HOME/smith/clang

capture "$smith" --smith-which -c hello.c
program=$(printf '%s\n' "$out" | head -n 1 | cut -d ' ' -f 3)
folder=${program%/bin/clang}
expect "which names a bin/clang in a folder of the data folder" "$clang_folders/" "${folder%/*}/"
expect "which writes nothing" no "$(exists "$XDG_DATA_HOME")"

capture "$smith" -fsyntax-only hello.c
expect "the first compile works" "0" "$rc"
expect "and writes the folder" yes "$(exists "$folder")"
capture "$smith" -print-resource-dir
major=${out##*/}
expect "the resource directory is in that folder" "$folder/lib/clang/$major" "$out"
for file in "lib/clang/$major/include/stddef.h" "lib/clang/$major/include/arm_neon.h" "lib/clang/$major/share/asan_ignorelist.txt" \
    "lib/clang/$major/lib/darwin/libclang_rt.asan_osx_dynamic.dylib" "lib/clang/$major/lib/darwin/libclang_rt.ubsan_osx_dynamic.dylib" \
    "lib/clang/$major/lib/darwin/libclang_rt.osx.a" "lib/clang/$major/lib/darwin/libclang_rt.profile_osx.a" "lib/libLTO.dylib" \
    bin/clang bin/clang++; do
    expect "the folder holds $file" yes "$(exists "$folder/$file")"
done
expect "iOS's runtimes are not carried" no "$(exists "$folder/lib/clang/$major/lib/darwin/libclang_rt.ios.a")"
expect "bin/clang is the smith that ran" yes "$([ "$folder/bin/clang" -ef "$smith" ] && echo yes || echo no)"
expect "no half-written folder is left" "" "$(ls "$clang_folders" | grep tmp)"

capture "$smith" -M hello.c
expect_contains "the builtin headers come from the folder" "$out" "$folder/lib/clang/$major/include/stddef.h"
capture "$smith" -### -c hello.c
expect "nothing names an LLVM install" "" "$(printf '%s\n%s\n' "$out" "$err" | grep -F 'smith/llvm' | head -n 1)"

# A second compile reads the folder and writes nothing.
stamp=$(ls -l "$folder/lib/clang/$major/include/stddef.h" "$folder/lib/libLTO.dylib")
sleep 1
capture "$smith" -fsyntax-only hello.c
expect "a second compile works" "0" "$rc"
expect "and rewrites no file" "$stamp" "$(ls -l "$folder/lib/clang/$major/include/stddef.h" "$folder/lib/libLTO.dylib")"

# -flto links through the system linker, which loads libLTO.dylib from the folder.
capture "$smith" -flto hello.c -o lto
expect "an -flto link works" "0 hi" "$rc $(./lto)"
expect "and the linker prints nothing about libLTO" "" "$err"

# Compiling and linking in one command runs the driver again with -cc1: bin/clang is smith. A smith copy takes the link,
# and removing the copy leaves nothing dangling for the next one.
cp "$smith" "$work/smith2"
capture "$work/smith2" hello.c -o prog
expect "a compile and a link with a copy of smith" "0 hi" "$rc $(./prog)"
expect "bin/clang is the copy now" yes "$([ "$folder/bin/clang" -ef "$work/smith2" ] && echo yes || echo no)"
rm "$work/smith2"
capture "$smith" hello.c -o prog
expect "after the copy is removed, a compile and a link still work" "0 hi" "$rc $(./prog)"
expect "and bin/clang is smith again" yes "$([ "$folder/bin/clang" -ef "$smith" ] && echo yes || echo no)"
ln -s "$smith" "$work/smith++"
printf '#include <vector>\n#include <cstdio>\nint main() { std::vector<int> v{1, 2, 3}; std::printf("%%zu\\n", v.size()); }\n' >v.cpp
capture "$work/smith++" v.cpp -o v
expect "smith++ compiles and links C++" "0 3" "$rc $(./v)"

# A sanitized program loads its runtime from the folder, which neither smith cache clean nor pruning nor a removed cache
# touches.
capture "$smith" -fsanitize=address,undefined -g hello.c -o asan
expect "a sanitized program builds and runs" "0 hi" "$rc $(./asan)"
rpath=$(otool -l asan | sed -n 's/^ *path \(.*clang.*\) (offset.*/\1/p')
expect "its rpath is the folder's runtime" "$folder/lib/clang/$major/lib/darwin" "$rpath"
capture "$smith" cache clean
expect "smith cache clean works" "0" "$rc"
expect "the folder is still there" yes "$(exists "$folder/lib/clang/$major/lib/darwin/libclang_rt.asan_osx_dynamic.dylib")"
expect "the sanitized program still runs" "hi" "$(./asan)"
rm -rf "$XDG_CACHE_HOME"
expect "with the whole cache removed it still runs" "hi" "$(./asan)"
printf 'int main(void) { int a[2] = {0, 0}; volatile int i = 2; return a[i]; }\n' >bad.c
"$smith" -fsanitize=address -g bad.c -o bad
expect "and still finds the runtime to report an error" "AddressSanitizer" "$(./bad 2>&1 | grep -o AddressSanitizer | head -n 1)"

# Processes that start together on a new data folder all work, and leave one folder.
before=$(ls "$clang_folders")
rm -rf "$XDG_DATA_HOME"
for n in 1 2 3 4 5 6 7 8; do
    ("$smith" -fsyntax-only hello.c 2>"$work/race$n" && echo ok >"$work/race$n.ok") &
done
wait
ok=$(ls "$work"/race*.ok | wc -l | tr -d ' ')
expect "eight first compiles at once all work" 8 "$ok"
expect "and leave the one folder, with the same name" "$before" "$(ls "$clang_folders")"

# Messages.
capture env -u XDG_DATA_HOME HOME=relative "$smith" -fsyntax-only hello.c
expect "no usable data folder: status 2" 2 "$rc"
expect_contains "and a message that names the variables" "$err" "set HOME or XDG_DATA_HOME"
capture env XDG_DATA_HOME=/dev/null/data "$smith" -fsyntax-only hello.c
expect "an unwritable data folder: status 2" 2 "$rc"
expect_contains "and a message that names it" "$err" "cannot create"

echo
echo "$total resource checks, $failures failed"
[ "$failures" -eq 0 ]
