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

# A program of the programs folder is a clone of the smith that made it: the same bytes in a file of its own, never a
# hard link (macOS's policy daemon kills a process when another name of its file is removed; issue #48).
is_copy() { # path, smith
    cmp -s "$1" "$2" && [ ! "$1" -ef "$2" ]
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

if [ "$("$smith" cc --smith-which -c a.c | head -n 1 | cut -d ' ' -f 1,2)" != "clang in-process" ]; then
    echo "skipped: this smith has no clang inside (the stage0)"
    exit 0
fi

mkdir "$work/src"
cd "$work/src" || exit 1
printf '#include <stddef.h>\n#include <stdio.h>\nint main(void) { puts("hi"); return 0; }\n' >hello.c
clang_folders=$XDG_DATA_HOME/smith/clang

capture "$smith" cc --smith-which -c hello.c
program=$(printf '%s\n' "$out" | head -n 1 | cut -d ' ' -f 3)
programs=${program%/bin/clang}
program_folders=$XDG_DATA_HOME/smith/programs
expect "which names a bin/clang in a folder of the programs folder" "$program_folders/" "${programs%/*}/"
expect "which writes nothing" no "$(exists "$XDG_DATA_HOME")"

capture "$smith" cc -fsyntax-only hello.c
expect "the first compile works" "0" "$rc"
capture "$smith" cc -print-resource-dir
major=${out##*/}
folder=${out%/lib/clang/*}
expect "the resource directory is in a folder of the runtime folders" "$clang_folders/" "${folder%/*}/"
expect "and the first compile writes the folder" yes "$(exists "$folder")"
expect "and the programs folder" yes "$(exists "$programs")"
for file in "lib/clang/$major/include/stddef.h" "lib/clang/$major/include/arm_neon.h" "lib/clang/$major/share/asan_ignorelist.txt" \
    "lib/clang/$major/lib/darwin/libclang_rt.asan_osx_dynamic.dylib" "lib/clang/$major/lib/darwin/libclang_rt.ubsan_osx_dynamic.dylib" \
    "lib/clang/$major/lib/darwin/libclang_rt.osx.a" "lib/clang/$major/lib/darwin/libclang_rt.profile_osx.a" "lib/libLTO.dylib" \
    ; do
    expect "the folder holds $file" yes "$(exists "$folder/$file")"
done
expect "the runtime folder holds no program" no "$(exists "$folder/bin")"
expect "the programs folder's lib is the runtime folder's" "$folder/lib" "$(readlink "$programs/lib")"
expect "iOS's runtimes are not carried" no "$(exists "$folder/lib/clang/$major/lib/darwin/libclang_rt.ios.a")"
for name in clang clang++ ld; do
    expect "bin/$name is a copy of the smith that ran, with a file of its own" yes "$(is_copy "$programs/bin/$name" "$smith" && echo yes || echo no)"
done
expect "no half-written folder is left" "" "$({ ls "$clang_folders"; ls "$program_folders"; } | grep tmp)"

capture "$smith" cc -M hello.c
expect_contains "the builtin headers come from the folder" "$out" "$folder/lib/clang/$major/include/stddef.h"
capture "$smith" cc -### -c hello.c
expect "nothing names an LLVM install" "" "$(printf '%s\n%s\n' "$out" "$err" | grep -F 'smith/llvm' | head -n 1)"

# A second compile reads the folder and writes nothing.
stamp=$(ls -l "$folder/lib/clang/$major/include/stddef.h" "$folder/lib/libLTO.dylib")
sleep 1
capture "$smith" cc -fsyntax-only hello.c
expect "a second compile works" "0" "$rc"
expect "and rewrites no file" "$stamp" "$(ls -l "$folder/lib/clang/$major/include/stddef.h" "$folder/lib/libLTO.dylib")"

# smith lint and smith lsp read the same folder for the builtin headers, not the LLVM install.
capture "$smith" lint --quiet --checks='-*,bugprone-sizeof-expression' --extra-arg=-v hello.c --
expect "smith lint parses a file that includes a builtin header" "0" "$rc"
expect_contains "and its builtin headers come from clang's folder" "$out$err" "$XDG_DATA_HOME/smith/programs/"
expect "and no LLVM install is named" "" "$(printf '%s\n%s\n' "$out" "$err" | grep -F 'smith/llvm' | head -n 1)"
printf '[{"directory": "%s", "file": "hello.c", "arguments": ["smith-cc", "-c", "hello.c"]}]\n' "$PWD" >compile_commands.json
printf 'Checks: "-*"\n' >.clang-tidy
capture "$smith" lsp --check=hello.c
expect "smith lsp --check works" "0" "$rc"
expect "and logs no error" "0" "$(printf '%s\n%s\n' "$out" "$err" | grep -c 'E\[')"
expect_contains "and its resource directory is in this smith's programs folder, which links clang's" "$out$err" "-resource-dir=$XDG_DATA_HOME/smith/programs/"
expect_contains "and it is lib/clang/$major" "$out$err" "/lib/clang/$major"
rm compile_commands.json

# -flto links with lld, which has LLVM inside it and so needs no libLTO.dylib; the driver still names the one in the folder
# for a linker that does, and SMITH_LINKER=/usr/bin/ld loads it.
capture "$smith" cc -flto hello.c -o lto
expect "an -flto link works" "0 hi" "$rc $(./lto)"
expect "and the linker prints nothing about libLTO" "" "$err"
capture env SMITH_LINKER=/usr/bin/ld "$smith" cc -flto hello.c -o lto_ld
expect "an -flto link with the system ld works" "0 hi" "$rc $(./lto_ld)"
expect "and it prints nothing" "" "$err"

# The program the driver runs as `ld` is smith, and the link is lld's, in that process. An lld binary says so in its
# LC_BUILD_VERSION: tool 4 (ld64's LD is 3), with LLVM's version.
tool_of() { otool -l "$1" | sed -n '/LC_BUILD_VERSION/,/version/p' | sed -n 's/^ *tool \(.*\)/\1/p'; }
capture "$smith" cc hello.c -o linked
expect "a link works" "0 hi" "$rc $(./linked)"
expect "and is lld's" 4 "$(tool_of linked)"
expect "and is signed, with no codesign step" "0" "$(codesign --verify linked >/dev/null 2>&1; echo $?)"
expect "and is signed ad hoc by the linker" 1 "$(codesign -dv linked 2>&1 | grep -c 'linker-signed')"
capture env SMITH_LINKER=/usr/bin/ld "$smith" cc hello.c -o linked_ld
expect "SMITH_LINKER=/usr/bin/ld links with ld" "0 hi 3" "$rc $(./linked_ld) $(tool_of linked_ld)"
capture env SMITH_LINKER=/nonexistent/ld "$smith" cc hello.c -o linked_none
expect "a SMITH_LINKER that is not there is an error" 1 "$rc"
expect_contains "and says what it is" "$err" "SMITH_LINKER"
printf 'int missing(void);\nint main(void) { return missing(); }\n' >undefined.c
capture "$smith" cc undefined.c -o undefined
expect "an undefined symbol fails" 1 "$rc"
expect_contains "with lld's message" "$err" "undefined symbol: missing"
expect "and leaves no output" no "$(exists undefined)"

# What ld links today, lld links: frameworks, a dynamic library, -dead_strip, an Objective-C selector stub, a dSYM.
printf '#import <Foundation/Foundation.h>\n#import <AppKit/AppKit.h>\n#import <Metal/Metal.h>\nint main(void) { @autoreleasepool { NSString *s = [NSString stringWithFormat:@"%%d", 7]; puts(s.UTF8String); (void)MTLCreateSystemDefaultDevice(); NSApplication *a = NSApp; (void)a; } return 0; }\n' >objc.m
capture "$smith" cc -fobjc-arc -g objc.m -framework Foundation -framework AppKit -framework Metal -Wl,-dead_strip -o objc
expect "a program with frameworks, ARC and -dead_strip" "0 7" "$rc $(./objc 2>/dev/null)"
expect "and its dSYM has the debug info" yes "$([ -f objc.dSYM/Contents/Resources/DWARF/objc ] && echo yes || echo no)"
capture "$smith" cc -dynamiclib hello.c -o libhello.dylib
expect "a dynamic library links" 0 "$rc"
printf 'extern void puts_from_library(void);\nint main(void) { puts_from_library(); return 0; }\n' >uselib.c
printf '#include <stdio.h>\nvoid puts_from_library(void) { puts("lib"); }\n' >lib.c
"$smith" cc -dynamiclib lib.c -o liblib.dylib
capture "$smith" cc uselib.c -L. -llib -Wl,-rpath,@loader_path -o uselib
expect "and a program links against it" "0 lib" "$rc $(./uselib)"

# Compiling and linking in one command runs the driver again with -cc1: bin/clang is smith. A copy of smith is the same
# smith (the same LC_UUID), so it uses the same programs, and the links outlive the smith they were made from.
cp "$smith" "$work/smith2"
capture "$work/smith2" cc hello.c -o prog
expect "a compile and a link with a copy of smith" "0 hi" "$rc $(./prog)"
expect "a copy of smith makes no programs of its own" 1 "$(ls "$program_folders" | wc -l | tr -d ' ')"
rm "$work/smith2"
capture "$smith" cc hello.c -o prog
expect "after the copy is removed, a compile and a link still work" "0 hi" "$rc $(./prog)"

# Two different smiths (another LC_UUID, as a rebuild has) never share or remake each other's programs, and keep working
# at once on the one runtime folder: each compiles and links an Objective-C program, many times, together.
cp "$smith" "$work/smith_b"
perl -0777 -pi -e 's/(\x1b\0\0\0\x18\0\0\0)(.{16})/$1 . ("\xab" x 16)/se' "$work/smith_b"
codesign -f -s - "$work/smith_b" >/dev/null 2>&1
printf '#import <Foundation/Foundation.h>\nint main(void) { @autoreleasepool { puts([NSString stringWithFormat:@"%%d", 7].UTF8String); } return 0; }\n' >objc2.m
capture "$work/smith_b" cc objc2.m -framework Foundation -o objc_b
expect "a second smith (another LC_UUID) works" "0 7" "$rc $(./objc_b)"
expect "and has programs folder of its own" 2 "$(ls "$program_folders" | wc -l | tr -d ' ')"
expect "that hold its own copies" yes "$(for f in "$program_folders"/*; do is_copy "$f/bin/ld" "$work/smith_b" && echo yes; done | head -n 1)"
expect "and the first smith's copies are still the first" yes "$(is_copy "$programs/bin/ld" "$smith" && echo yes || echo no)"
for n in 1 2 3 4 5 6 7 8; do
    (for round in 1 2 3 4 5; do
        "$smith" cc objc2.m -framework Foundation -o "objc_a$n" 2>>"$work/mix$n" && [ "$(./objc_a$n)" = 7 ] || echo bad >>"$work/mix$n.bad"
        "$work/smith_b" cc objc2.m -framework Foundation -o "objc_c$n" 2>>"$work/mix$n" && [ "$(./objc_c$n)" = 7 ] || echo bad >>"$work/mix$n.bad"
    done) &
done
wait
expect "eight workers of two smiths, five rounds each, all link and run" 0 "$(cat "$work"/mix*.bad 2>/dev/null | wc -l | tr -d ' ')"
expect "and the programs are still each its own" yes "$(is_copy "$programs/bin/clang" "$smith" && is_copy "$programs/bin/ld" "$smith" && echo yes || echo no)"
expect "and no half-made folder is left" "" "$(ls "$program_folders" | grep tmp)"

# A programs folder idle for more than 31 days is removed when a new one is made; a recent one, and the runtime folder, stay.
cp "$work/smith_b" "$work/smith_c"
perl -0777 -pi -e 's/(\x1b\0\0\0\x18\0\0\0)(.{16})/$1 . ("\xcd" x 16)/se' "$work/smith_c"
codesign -f -s - "$work/smith_c" >/dev/null 2>&1
old=$(for f in "$program_folders"/*; do is_copy "$f/bin/ld" "$work/smith_b" && echo "$f"; done)
touch -t 202001010000 "$old"
mkdir "$program_folders/stale.99.tmp"
touch -t 202001010000 "$program_folders/stale.99.tmp"
capture "$work/smith_c" cc -fsyntax-only hello.c
expect "an idle programs folder is removed when a new one is made" no "$(exists "$old")"
expect "so is an old half-made one" no "$(exists "$program_folders/stale.99.tmp")"
expect "a recent one stays" yes "$(exists "$programs")"
expect "and the runtime folder" yes "$(exists "$folder")"
# A programs folder records its binary (LC_UUID and path). When a new one is made, the folder of a binary that is gone
# is removed, unless a running smith holds its lock (a long-running `smith lsp` of a deleted worktree) or it is younger
# than an hour; one whose binary exists stays.
variant() { # name, byte: a copy of smith with another LC_UUID
    cp "$smith" "$work/$1"
    perl -0777 -pi -e 's/(\x1b\0\0\0\x18\0\0\0)(.{16})/$1 . ("\x'"$2"'" x 16)/se' "$work/$1"
    codesign -f -s - "$work/$1" >/dev/null 2>&1
}
folder_of() { # a smith: the programs folder it made
    for f in "$program_folders"/*; do is_copy "$f/bin/ld" "$1" && echo "$f"; done
}
two_hours_ago=$(date -v-2H +%Y%m%d%H%M)
variant smith_d 11
variant smith_e 22
"$work/smith_d" cc -fsyntax-only hello.c
"$work/smith_e" cc -fsyntax-only hello.c
folder_d=$(folder_of "$work/smith_d")
folder_e=$(folder_of "$work/smith_e")
expect "a programs folder records its binary" smith_d "$(sed -n 2p "$folder_d/binary" | sed 's,.*/,,')"
(sleep 20 | "$work/smith_d" lsp >/dev/null 2>&1) &
holder=$!
tries=0
while [ $tries -lt 100 ] && ! [ -f "$folder_d/.in-use" ]; do sleep 0.1; tries=$((tries + 1)); done
sleep 1
rm "$work/smith_d"
touch -t "$two_hours_ago" "$folder_d" "$folder_e"
variant smith_f 33
"$work/smith_f" cc -fsyntax-only hello.c
expect "a folder whose smith is running stays when its binary is gone" yes "$(exists "$folder_d")"
expect "and one whose binary exists is never touched" yes "$(exists "$folder_e")"
kill $holder 2>/dev/null
wait $holder 2>/dev/null
for p in $(lsof -t "$folder_d/.in-use" 2>/dev/null); do kill "$p" 2>/dev/null; done
for _ in $(seq 1 300); do # until nothing holds the file: order, not a number of seconds
    [ -z "$(lsof -t "$folder_d/.in-use" 2>/dev/null)" ] && break
    sleep 0.1
done
touch -t "$two_hours_ago" "$folder_d"
variant smith_g 44
"$work/smith_g" cc -fsyntax-only hello.c
expect "once that smith has died the next prune removes its folder" no "$(exists "$folder_d")"
expect "and a folder whose binary exists still stays" yes "$(exists "$folder_e")"
rm "$work/smith_e"
touch -t "$(date -v-30M +%Y%m%d%H%M)" "$folder_e"
variant smith_h 55
"$work/smith_h" cc -fsyntax-only hello.c
expect "a folder younger than an hour stays even when its binary is gone" yes "$(exists "$folder_e")"
rm -f "$work"/smith_[d-h]

ln -s "$smith" "$work/smith-c++"
printf '#include <vector>\n#include <cstdio>\nint main() { std::vector<int> v{1, 2, 3}; std::printf("%%zu\\n", v.size()); }\n' >v.cpp
capture "$work/smith-c++" v.cpp -o v
expect "smith-c++ compiles and links C++" "0 3" "$rc $(./v)"

# A sanitized program loads its runtime from the folder, which neither smith cache clean nor pruning nor a removed cache
# touches.
capture "$smith" cc -fsanitize=address,undefined -g hello.c -o asan
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
"$smith" cc -fsanitize=address -g bad.c -o bad
expect "and still finds the runtime to report an error" "AddressSanitizer" "$(./bad 2>&1 | grep -o AddressSanitizer | head -n 1)"

# Processes that start together on a new data folder all work, and leave one folder.
before=$(ls "$clang_folders")
rm -rf "$XDG_DATA_HOME"
for n in 1 2 3 4 5 6 7 8; do
    ("$smith" cc -fsyntax-only hello.c 2>"$work/race$n" && echo ok >"$work/race$n.ok") &
done
wait
ok=$(ls "$work"/race*.ok | wc -l | tr -d ' ')
expect "eight first compiles at once all work" 8 "$ok"
expect "and leave the one folder, with the same name" "$before" "$(ls "$clang_folders")"
expect "and one programs folder" 1 "$(ls "$program_folders" | wc -l | tr -d ' ')"

# Messages.
capture env -u XDG_DATA_HOME HOME=relative "$smith" cc -fsyntax-only hello.c
expect "no usable data folder: status 2" 2 "$rc"
expect_contains "and a message that names the variables" "$err" "set HOME or XDG_DATA_HOME"
capture env XDG_DATA_HOME=/dev/null/data "$smith" cc -fsyntax-only hello.c
expect "an unwritable data folder: status 2" 2 "$rc"
expect_contains "and a message that names it" "$err" "cannot create"

echo
echo "$total resource checks, $failures failed"
[ "$failures" -eq 0 ]
