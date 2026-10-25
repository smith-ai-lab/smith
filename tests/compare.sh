#!/bin/sh
# Runs a table of invocations through a real compiler and through smith (`smith cc` and the link smith-cc), and compares what each prints, how each exits
# and which files each writes. Both run in the same directory, one after the other, with the same relative paths.
# Usage: sh tests/compare.sh path/to/smith /path/to/clang
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
real=$2
realdir=$(dirname "$real")
realname=$(basename "$real")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
cd "$work" || exit 1
mkdir log links
ln -s "$smith" links/smith-cc # the compiler also runs by this name
printf '#include <stdio.h>\nint main(void) { puts("hi"); return 0; }\n' >hello.c
printf 'int main(void) { return missing; }\n' >bad.c
printf -- '-c hello.c -o rsp.o\n' >opts.rsp
printf 'int other(void) { return 3; }\n' >other.c
printf 'int add(int a, int b) { return a + b; }\n' >lib.cpp
printf '.text\n.globl _f\n_f:\n ret\n' >src.s
printf '#define R ret\n.text\n.globl _g\n_g:\n R\n' >src.S
printf '#import <Foundation/Foundation.h>\nint main(void) { @autoreleasepool { NSLog(@"x"); } return 0; }\n' >obj.m
printf 'int main(void) { int x = 1; return x == 1 \n' >syntax.c
cat >cases.txt <<'CASES'
--version
-E hello.c
-c hello.c -o out.o -MMD -MF out.d
-fsyntax-only bad.c
nonexistent.c
-c -x c - -o stdin.o < hello.c
-E -x c - < hello.c
@opts.rsp
-### -c hello.c
-print-search-dirs
-print-resource-dir
-print-file-name=libclang_rt.asan_osx_dynamic.dylib
-v -c hello.c -o out.o
-c -O2 -g -Wall -Wextra -std=gnu23 hello.c -o out.o
-c -O1 -fsanitize=address,undefined -g hello.c -o out.o
-S hello.c -o out_a.s
-S -emit-llvm -O1 hello.c -o out_a.ll
-c hello.c other.c
-c src.s -o out.o
-c src.S -o out.o
-M hello.c
-MM -MG -MT t hello.c
-E -P -dD hello.c
-fsyntax-only -Weverything hello.c
-fsyntax-only -fcolor-diagnostics syntax.c
-fsyntax-only -fdiagnostics-color=always -Werror bad.c
-x c++ -std=c++20 -fsyntax-only lib.cpp
-x c++ -c lib.cpp -o out.o
-c -x objective-c -fobjc-arc obj.m -o out.o
hello.c -o prog
hello.c other.c -o prog
-g hello.c -o prog
-fsanitize=address,undefined -g hello.c -o prog
-O2 -flto -c hello.c -o out.o
-c -g -ffile-prefix-map=/ignored=/mapped hello.c -o out.o
CASES

# On macOS smith picks the SDK through the environment (see smith_sdk_environment in smith.c): Homebrew's clang's default
# config would force the Command Line Tools SDK. So the direct run gets the same two variables, and everything else
# (arguments, files, output, exit status) must match byte for byte.
direct_env() {
    if [ "$(uname -s)" = Darwin ]; then
        CLANG_NO_DEFAULT_CONFIG=1
        SDKROOT=$(xcrun --show-sdk-path)
        export CLANG_NO_DEFAULT_CONFIG SDKROOT
    fi
}

# Three things differ between two builds of the same LLVM, and only in text: where each one lives, and the name of the
# program it prints for itself, which is the symlink smith was given (clang) where the clang binary prints what it points
# to (clang-23): -v, --version, -###, -print-search-dirs. Each is rewritten to one name, so that everything else is
# compared. A Homebrew clang also says "Homebrew clang version", in the text and in the producer of debug info and bitcode,
# so against it only the cases without -g, -emit-llvm and -flto are byte for byte; the default REAL is therefore the clang
# in the LLVM that smith links, which every case must match.
real_prefix=$(cd "$realdir/.." && pwd -P)
smith_prefix=$real_prefix
smith_runtime=
inproc=$("$smith" cc --smith-which -c a.c | head -n 1)
# A smith with clang inside links with lld, which Apple's ld and lld write differently, so the programs are compared by what
# they print, and the real clang is told the linker's version that smith tells the driver for lld (shim/clang.cpp), since
# the version of the `ld` it was built beside makes it use `objc_msgSendClass$` stubs, which lld does not write.
real_flags=
case $inproc in
"clang in-process "*)
    smith_prefix=${inproc#clang in-process }
    smith_prefix=${smith_prefix%/bin/clang}
    # The programs (bin/clang) and the files the driver reads (lib/clang) are in two folders; both are the real LLVM here.
    smith_runtime=$("$smith" cc -print-resource-dir)
    smith_runtime=${smith_runtime%/lib/clang/*}
    real_flags=-mlinker-version=1115.7.3
    ;;
esac
normalize() { # via
    prefix=$real_prefix
    [ "$1" != direct ] && prefix=$smith_prefix
    runtime=$prefix
    [ "$1" != direct ] && [ -n "$smith_runtime" ] && runtime=$smith_runtime
    for kind in out err; do
        sed -e "s|$prefix|LLVM|g" -e "s|$runtime|LLVM|g" -e 's|^Homebrew clang version|clang version|' \
            -e 's| -stack-usage-file [^ ]*||' \
            -e 's|LLVM/bin/clang-[0-9]*|LLVM/bin/clang|g' "log/$kind.$1" >"log/$kind.$1.n"
        mv "log/$kind.$1.n" "log/$kind.$1"
    done
}

run_one() { # via, arguments
    rm -rf ./*.o ./*.d ./out_* ./prog* ./*.dSYM
    case $1 in
    direct) (
        PATH="$realdir:$PATH"
        direct_env
        eval "$realname $real_flags $2"
    ) </dev/null >log/out.$1 2>log/err.$1 ;;
    smith | link) (
        # A smith with clang linked in compiles in its own process, which is what is compared; the stage0 runs $real.
        if "$smith" cc --smith-which -c a.c | grep -q '^clang in-process'; then
            unset SMITH_CLANG
        else
            SMITH_CLANG=$real
            export SMITH_CLANG
        fi
        if [ "$1" = smith ]; then eval "\"$smith\" cc $2"; else eval "\"$work/links/smith-cc\" $2"; fi
    ) </dev/null >log/out.$1 2>log/err.$1 ;;
    esac
    echo $? >log/rc.$1
    normalize "$1"
    for f in ./*.o ./*.d ./out_* ./prog* ./*.dSYM/Contents/Resources/DWARF/*; do
        # smith's own `<program>.stack` (the worst-case stack, issue #207) is no file a compiler writes.
        case $f in ./prog*.stack) continue ;; esac
        if [ -f "$f" ]; then
            echo "$f"
            # A linked program with debug info holds the time of each object it was linked from (the debug map), so only
            # its size can be compared.
            # With lld the linked program, and the dSYM made from its debug map, differ in bytes from ld's.
            case " $2 $f" in
            *./prog* | *.dSYM/*)
                if [ -n "$real_flags" ]; then
                    case $f in ./prog*) "$f" </dev/null 2>&1 | head -n 1 ;; esac
                else
                    case " $2 $f" in *" -g "*/prog) wc -c <"$f" ;; *) cksum <"$f" ;; esac
                fi
                ;;
            *) cksum <"$f" ;;
            esac
        fi
    done >log/files.$1
}

failures=0
while read -r line <&3; do
    run_one direct "$line"
    run_one smith "$line"
    run_one link "$line"
    bad=
    for via in smith link; do
        for kind in out err rc files; do
            cmp -s "log/$kind.direct" "log/$kind.$via" || bad="$bad $via:$kind"
        done
    done
    if [ -z "$bad" ]; then
        echo "ok    $line"
    else
        failures=$((failures + 1))
        echo "FAIL  $line  (differs in:$bad)"
        for entry in $bad; do diff "log/${entry#*:}.direct" "log/${entry#*:}.${entry%:*}" | head -6; done
    fi
done 3<cases.txt
echo
if [ "$failures" -eq 0 ]; then echo "smith matches $realname on every case"; else echo "$failures cases differ"; fi
[ "$failures" -eq 0 ]
