#!/bin/sh
# Routing tests for smith, run against stub compilers so that no clang or Xcode is needed and no real build is touched.
# Usage: sh tests/run.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")" && pwd)
cc=${CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
mkdir "$work/bin"
for name in clang clang++ xcrun metal; do
    $cc -std=gnu23 -DNAME="\"$name\"" "$here/stub.c" -o "$work/bin/$name" || exit 1
done
ln -s "$smith" "$work/smith++"
ln -s "$smith" "$work/bin/selfclang"
cl="$work/bin/clang"

failures=0
out=
err=
rc=0

# Runs a command with its stdout in $out, its stderr in $err and its exit status in $rc.
capture() {
    out=$("$@" 2>"$work/err")
    rc=$?
    err=$(cat "$work/err")
}

lines() {
    printf '%s\n' "$@"
}

expect() { # name, expected, actual
    if [ "$2" = "$3" ]; then
        echo "ok    $1"
    else
        failures=$((failures + 1))
        echo "FAIL  $1"
        echo "  expected: $2"
        echo "  actual:   $3"
    fi
}

capture env SMITH_CLANG="$cl" "$smith" -c "a b.c" "" -DX="y z" -o out.o
expect "clang route forwards every argument untouched" "$(lines "clang argv0=$cl" '[-c]' '[a b.c]' '[]' '[-DX=y z]' '[-o]' '[out.o]')" "$out"
expect "clang route: stderr passes through" "clang-stderr" "$err"
expect "clang route: status 0" 0 "$rc"

capture env STUB_EXIT=7 SMITH_CLANG="$cl" "$smith" -c a.c
expect "exit status is preserved" 7 "$rc"

capture env STUB_SIGNAL=15 SMITH_CLANG="$cl" "$smith" -c a.c
expect "death by signal is preserved (128 + 15)" 143 "$rc"

capture env SMITH_CLANG="$cl" "$smith" --version
expect "--version goes to the compiler" "$(lines "clang argv0=$cl" '[--version]')" "$out"

capture env SMITH_CLANG="$cl" "$smith" @r.rsp a.c
expect "response files are forwarded untouched" "$(lines "clang argv0=$cl" '[@r.rsp]' '[a.c]')" "$out"

capture env SMITH_CLANG="$cl" "$smith" -I shaders.metal -c a.c
expect "a flag value ending in .metal is not an input" "$(lines "clang argv0=$cl" '[-I]' '[shaders.metal]' '[-c]' '[a.c]')" "$out"

capture env PATH="$work/bin:$PATH" "$smith" -o shaders.metallib a.metal b.metal
expect "metal route goes through xcrun -sdk macosx metal" "$(lines "xcrun argv0=$work/bin/xcrun" '[-sdk]' '[macosx]' '[metal]' '[-o]' '[shaders.metallib]' '[a.metal]' '[b.metal]')" "$out"

capture env SMITH_METAL="$work/bin/metal" "$smith" -E a.metal
expect "SMITH_METAL replaces xcrun" "$(lines "metal argv0=$work/bin/metal" '[-E]' '[a.metal]')" "$out"

capture env SMITH_CLANG="$cl" "$smith" a.metal b.c
expect "mixed .metal and C inputs are refused" "smith: .metal and other inputs need different compilers and flags; compile the shaders in their own command" "$err"
expect "mixed inputs: status 2 and nothing ran" "2 " "$rc $out"

capture env SMITH_CLANG="$cl" "$smith" -c a.smith -o out.o
expect "a .smith input goes to clang with its arguments untouched" "$(lines "clang argv0=$cl" '[-c]' '[a.smith]' '[-o]' '[out.o]')" "$out"

capture env SMITH_CLANGXX="$work/bin/clang++" "$work/smith++" -c a.cpp
expect "smith++ uses clang++" "$(lines "clang++ argv0=$work/bin/clang++" '[-c]' '[a.cpp]')" "$out"

if [ "$(uname -s)" = Darwin ]; then
    capture env -u SMITH_CLANG "$smith" --smith-which -c a.c
    which_identity=$(printf '%s\n' "$out" | tail -n +2)
    out=$(printf '%s\n' "$out" | head -n 1)
    case $out in
    "clang in-process "/*/bin/clang)
        # A smith with clang linked in compiles in its own process unless SMITH_CLANG names a clang to run instead.
        llvm_clang=${out#clang in-process }
        # The program is in the folder smith writes clang's files to on first use, which --smith-which does not make.
        case $llvm_clang in
        */smith/clang/llvm-*/bin/clang) found=yes ;;
        *) found=$llvm_clang ;;
        esac
        expect "in-process: the default is clang inside smith, run as the bin/clang of its own folder of clang's files" yes "$found"
        case $which_identity in
        "identity llvm-"*" "*) expect "in-process: --smith-which names the identity of the clang inside" yes yes ;;
        *) expect "in-process: --smith-which names the identity of the clang inside" "identity llvm-<version> <hash>" "$which_identity" ;;
        esac
        capture env -u SMITH_CLANGXX "$work/smith++" --smith-which -c a.cpp
        expect "in-process: smith++ is clang++ inside smith" "clang in-process ${llvm_clang}++" "$(printf '%s\n' "$out" | head -n 1)"
        capture env SMITH_CLANG="$cl" "$smith" --smith-which -c a.c
        expect "in-process: SMITH_CLANG still runs an external clang" "clang $cl" "$out"
        capture env -u SMITH_CLANG SMITH_CLANGXX="$work/bin/clang++" "$smith" --smith-which -c a.c
        expect "in-process: SMITH_CLANGXX does not affect smith" "clang in-process" "${out%% /*}" ;;
    *)
        if [ -x /opt/homebrew/opt/llvm/bin/clang ]; then
            expect "on macOS the default is Homebrew's clang" "clang /opt/homebrew/opt/llvm/bin/clang" "$out"
            capture env -u SMITH_CLANGXX "$work/smith++" --smith-which -c a.cpp
            expect "on macOS smith++ defaults to Homebrew's clang++" "clang /opt/homebrew/opt/llvm/bin/clang++" "$out"
        fi
        if [ ! -x /opt/homebrew/opt/llvm/bin/clang ] && [ ! -x /usr/local/opt/llvm/bin/clang ]; then
            capture env -u SMITH_CLANG "$smith" -c a.c
            expect "macOS without Homebrew's llvm: the error names brew install llvm and SMITH_CLANG" \
                "smith: cannot find clang in /opt/homebrew/opt/llvm/bin: run \`brew install llvm\`, or set SMITH_CLANG" "$err"
        fi ;;
    esac
    # The SDK is chosen through the environment, never a flag, and only for what the caller left unset.
    capture env -u SDKROOT -u CLANG_NO_DEFAULT_CONFIG PATH="$work/bin:$PATH" STUB_SDK=/stub/sdk STUB_SHOW_ENV=1 \
        SMITH_CLANG="$cl" "$smith" -c a.c
    expect "macOS: SDKROOT comes from xcrun and the default config is off, with no flag added" \
        "$(lines 'env SDKROOT=/stub/sdk CLANG_NO_DEFAULT_CONFIG=1' "clang argv0=$cl" '[-c]' '[a.c]')" "$out"
    capture env -u CLANG_NO_DEFAULT_CONFIG SDKROOT=/mine PATH="$work/bin:$PATH" STUB_SDK=/stub/sdk STUB_SHOW_ENV=1 \
        SMITH_CLANG="$cl" "$smith" -c a.c
    expect "macOS: the caller's SDKROOT wins, and the default config is still off" \
        "$(lines 'env SDKROOT=/mine CLANG_NO_DEFAULT_CONFIG=1' "clang argv0=$cl" '[-c]' '[a.c]')" "$out"
    capture env -u SDKROOT CLANG_NO_DEFAULT_CONFIG=0 PATH="$work/bin:$PATH" STUB_SDK=/stub/sdk STUB_SHOW_ENV=1 \
        SMITH_CLANG="$cl" "$smith" -c a.c
    expect "macOS: the caller's CLANG_NO_DEFAULT_CONFIG wins" \
        "$(lines 'env SDKROOT=/stub/sdk CLANG_NO_DEFAULT_CONFIG=0' "clang argv0=$cl" '[-c]' '[a.c]')" "$out"
    capture env -u SDKROOT -u CLANG_NO_DEFAULT_CONFIG PATH="$work/bin:$PATH" STUB_SDK=/stub/sdk STUB_XCRUN_FAIL=1 \
        STUB_SHOW_ENV=1 SMITH_CLANG="$cl" "$smith" -c a.c
    expect "macOS: when xcrun fails SDKROOT stays unset" \
        "$(lines 'env SDKROOT=(unset) CLANG_NO_DEFAULT_CONFIG=1' "clang argv0=$cl" '[-c]' '[a.c]')" "$out"
    capture env -u SDKROOT -u CLANG_NO_DEFAULT_CONFIG PATH="$work/bin:$PATH" STUB_SDK=/stub/sdk STUB_SHOW_ENV=1 \
        SMITH_CLANG="$cl" "$smith" -isysroot /mine/sdk -c a.c
    expect "macOS: the caller's -isysroot is forwarded untouched" \
        "$(lines 'env SDKROOT=/stub/sdk CLANG_NO_DEFAULT_CONFIG=1' "clang argv0=$cl" '[-isysroot]' '[/mine/sdk]' '[-c]' '[a.c]')" "$out"
fi

capture env SMITH_CLANG="$work/bin/selfclang" "$smith" a.c
expect "a compiler that is smith itself is refused" "smith: refers to smith itself: $work/bin/selfclang" "$err"
expect "self refusal: status 127" 127 "$rc"

capture env SMITH_CLANG=/nonexistent/clang "$smith" a.c
expect "a missing compiler is reported" "smith: not an executable: /nonexistent/clang" "$err"

capture env SMITH_CLANG="$cl" "$smith" --smith-which -c a.c
expect "--smith-which names the clang backend" "clang $cl" "$out"

capture env PATH="$work/bin:$PATH" "$smith" --smith-which a.metal
expect "--smith-which names the metal backend" "metal $work/bin/xcrun -sdk macosx metal" "$out"

capture env SMITH_VERBOSE=1 SMITH_CLANG="$cl" "$smith" -c a.c
expect "SMITH_VERBOSE shows the command" "$(lines "smith: exec $cl -c a.c" 'clang-stderr')" "$err"

echo
if [ "$failures" -eq 0 ]; then echo "all routing tests passed"; else echo "$failures failed"; fi
[ "$failures" -eq 0 ]
