#!/bin/sh
# Runs a table of invocations through a real compiler and through smith, and compares what each prints, how each exits
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
mkdir log
printf '#include <stdio.h>\nint main(void) { puts("hi"); return 0; }\n' >hello.c
printf 'int main(void) { return missing; }\n' >bad.c
printf -- '-c hello.c -o rsp.o\n' >opts.rsp
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

run_one() { # via, arguments
    rm -f ./*.o ./*.d
    case $1 in
    direct) (
        PATH="$realdir:$PATH"
        direct_env
        eval "$realname $2"
    ) </dev/null >log/out.$1 2>log/err.$1 ;;
    smith) (
        SMITH_CLANG=$real
        export SMITH_CLANG
        eval "\"$smith\" $2"
    ) </dev/null >log/out.$1 2>log/err.$1 ;;
    esac
    echo $? >log/rc.$1
    for f in ./*.o ./*.d; do
        if [ -f "$f" ]; then
            echo "$f"
            cksum <"$f"
        fi
    done >log/files.$1
}

failures=0
while read -r line <&3; do
    run_one direct "$line"
    run_one smith "$line"
    bad=
    for kind in out err rc files; do
        cmp -s "log/$kind.direct" "log/$kind.smith" || bad="$bad $kind"
    done
    if [ -z "$bad" ]; then
        echo "ok    $line"
    else
        failures=$((failures + 1))
        echo "FAIL  $line  (differs in:$bad)"
        for kind in $bad; do diff "log/$kind.direct" "log/$kind.smith" | head -6; done
    fi
done 3<cases.txt
echo
if [ "$failures" -eq 0 ]; then echo "smith matches $realname on every case"; else echo "$failures cases differ"; fi
[ "$failures" -eq 0 ]
