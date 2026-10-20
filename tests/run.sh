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
    expect "on macOS the default is Apple's clang" "clang /usr/bin/clang" "$out"
    capture env -u SMITH_CLANGXX "$work/smith++" --smith-which -c a.cpp
    expect "on macOS smith++ defaults to Apple's clang++" "clang /usr/bin/clang++" "$out"
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
