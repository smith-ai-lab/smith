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

# -flto links with lld, which has LLVM inside it and so needs no libLTO.dylib; the driver still names the one in the folder
# for a linker that does, and SMITH_LINKER=/usr/bin/ld loads it.
capture "$smith" -flto hello.c -o lto
expect "an -flto link works" "0 hi" "$rc $(./lto)"
expect "and the linker prints nothing about libLTO" "" "$err"
capture env SMITH_LINKER=/usr/bin/ld "$smith" -flto hello.c -o lto_ld
expect "an -flto link with the system ld works" "0 hi" "$rc $(./lto_ld)"
expect "and it prints nothing" "" "$err"

# The program the driver runs as `ld` is smith, and the link is lld's, in that process. An lld binary says so in its
# LC_BUILD_VERSION: tool 4 (ld64's LD is 3), with LLVM's version.
tool_of() { otool -l "$1" | sed -n '/LC_BUILD_VERSION/,/version/p' | sed -n 's/^ *tool \(.*\)/\1/p'; }
expect "bin/ld is the smith that ran" yes "$([ "$folder/bin/ld" -ef "$smith" ] && echo yes || echo no)"
capture "$smith" hello.c -o linked
expect "a link works" "0 hi" "$rc $(./linked)"
expect "and is lld's" 4 "$(tool_of linked)"
expect "and is signed, with no codesign step" "0" "$(codesign --verify linked >/dev/null 2>&1; echo $?)"
expect "and is signed ad hoc by the linker" 1 "$(codesign -dv linked 2>&1 | grep -c 'linker-signed')"
capture env SMITH_LINKER=/usr/bin/ld "$smith" hello.c -o linked_ld
expect "SMITH_LINKER=/usr/bin/ld links with ld" "0 hi 3" "$rc $(./linked_ld) $(tool_of linked_ld)"
capture env SMITH_LINKER=/nonexistent/ld "$smith" hello.c -o linked_none
expect "a SMITH_LINKER that is not there is an error" 1 "$rc"
expect_contains "and says what it is" "$err" "SMITH_LINKER"
printf 'int missing(void);\nint main(void) { return missing(); }\n' >undefined.c
capture "$smith" undefined.c -o undefined
expect "an undefined symbol fails" 1 "$rc"
expect_contains "with lld's message" "$err" "undefined symbol: missing"
expect "and leaves no output" no "$(exists undefined)"

# What ld links today, lld links: frameworks, a dynamic library, -dead_strip, an Objective-C selector stub, a dSYM.
printf '#import <Foundation/Foundation.h>\n#import <AppKit/AppKit.h>\n#import <Metal/Metal.h>\nint main(void) { @autoreleasepool { NSString *s = [NSString stringWithFormat:@"%%d", 7]; puts(s.UTF8String); (void)MTLCreateSystemDefaultDevice(); NSApplication *a = NSApp; (void)a; } return 0; }\n' >objc.m
capture "$smith" -fobjc-arc -g objc.m -framework Foundation -framework AppKit -framework Metal -Wl,-dead_strip -o objc
expect "a program with frameworks, ARC and -dead_strip" "0 7" "$rc $(./objc 2>/dev/null)"
expect "and its dSYM has the debug info" yes "$([ -f objc.dSYM/Contents/Resources/DWARF/objc ] && echo yes || echo no)"
capture "$smith" -dynamiclib hello.c -o libhello.dylib
expect "a dynamic library links" 0 "$rc"
printf 'extern void puts_from_library(void);\nint main(void) { puts_from_library(); return 0; }\n' >uselib.c
printf '#include <stdio.h>\nvoid puts_from_library(void) { puts("lib"); }\n' >lib.c
"$smith" -dynamiclib lib.c -o liblib.dylib
capture "$smith" uselib.c -L. -llib -Wl,-rpath,@loader_path -o uselib
expect "and a program links against it" "0 lib" "$rc $(./uselib)"

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
