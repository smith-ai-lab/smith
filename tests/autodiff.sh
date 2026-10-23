#!/bin/sh
# End-to-end tests for smith::autodiff (Enzyme, run at link time inside smith's lld): a toy across .smith and C files in
# the three profiles and under `smith test`, a copy of learner's scan reference against its hand-written backward,
# the commands of a program that does not differentiate, and the refusal of a smith with no Enzyme.
# Usage: sh tests/autodiff.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
mkdir "$work/bin" "$work/home"
ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" && ln -s "$smith" "$work/bin/smith-c++"
PATH=$work/bin:$PATH
export HOME=$work/home
export XDG_CACHE_HOME=$work/xdg-cache
export XDG_DATA_HOME=$work/xdg-data

failures=0
out=
err=
rc=0

capture() {
    out=$("$@" 2>"$work/err")
    rc=$?
    err=$(cat "$work/err")
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

expect_contains() { # name, text, needle
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

expect_lacks() { # name, text, needle
    case $2 in
    *"$3"*)
        failures=$((failures + 1))
        echo "FAIL  $1"
        echo "  expected not to contain: $3"
        ;;
    *) echo "ok    $1" ;;
    esac
}

# The lines "<name> <derivative> <analytic>" a program prints agree to 1e-9 relative: how many do not.
disagree() { awk '/^(reverse|forward|loss)/ { d = $(NF-1); a = $NF; e = d - a; if (e < 0) e = -e; s = a < 0 ? -a : a; if (e > 1e-9 * (s > 1 ? s : 1)) bad++ } END { print bad + 0 }'; }

# 1. The toy: main.smith, toy.smith, kernel.c and a test, which differentiate across three files.
mkdir "$work/toy" && cd "$work/toy" || exit 1
cp -R "$here/tests/autodiff/toy/." .
git init -q . && git config user.email t@t && git config user.name t
capture smith build
if [ "$rc" -ne 0 ] && echo "$err$out" | grep -q 'needs a smith with Enzyme'; then
    echo "ok    this smith has no Enzyme: the build says so"
    echo "skip  the rest of the tests need the full smith with Enzyme"
    exit "$failures"
fi
expect "toy: debug builds" 0 "$rc"
capture smith build release fast
expect "toy: release and fast build" 0 "$rc"
for profile in debug release fast; do
    capture "build/$profile/toy"
    expect "toy: $profile runs" 0 "$rc"
    expect "toy: $profile reverse and forward derivatives equal the analytic ones" 0 "$(printf '%s\n' "$out" | disagree)"
    expect "toy: $profile prints six lines" 6 "$(printf '%s\n' "$out" | wc -l | tr -d ' ')"
    expect "toy: $profile prints nothing of Enzyme's own to standard error" "" "$err"
done
commands=$(smith build -t commands build/debug/toy build/release/toy build/fast/toy)
expect_contains "toy: every compile has no optimization passes" "$commands" "-Xclang -disable-llvm-passes"
expect_contains "toy: every link is full LTO" "$commands" "smith-cc -flto=full build/release/autodiff/main.o"
expect_lacks "toy: ThinLTO is never used" "$commands" "-flto=thin"
expect_lacks "toy: the sanitizers are left out of the debug profile" "$commands" "-fsanitize"
expect_contains "toy: the objects are beside the others" "$commands" "build/debug/autodiff/kernel.o"
capture smith test
expect "toy: smith test, whose test differentiates, passes" 0 "$rc"
expect_contains "toy: ... and runs it" "$out" "1 passed, 0 failed"
capture smith lint
expect "toy: smith lint is clean" 0 "$rc"

# 2. A program that does not differentiate keeps its commands: sanitizers and ThinLTO as before, and no autodiff object.
mkdir "$work/plain" && cd "$work/plain" || exit 1
printf 'int main(void) {\n    return 0;\n}\n' >main.c
git init -q .
capture smith build release fast
commands=$(smith build -t commands build/debug/plain build/release/plain build/fast/plain)
expect "plain: release and fast build" 0 "$rc"
expect_contains "plain: debug has the sanitizers" "$commands" "-fsanitize=address,undefined"
expect_contains "plain: release links with ThinLTO" "$commands" "-flto=thin"
expect_lacks "plain: no full LTO" "$commands" "-flto=full"
expect_lacks "plain: no pass is disabled" "$commands" "disable-llvm-passes"
expect_lacks "plain: no autodiff object" "$commands" "autodiff/"

# 3. learner's scan reference: Enzyme's gradient equals the hand-written backward to 1e-12 (the program exits 1 if not).
mkdir "$work/scan" && cd "$work/scan" || exit 1
cp -R "$here/tests/autodiff/scan/base" "$here/tests/autodiff/scan/main.c" .
git init -q .
capture smith build release
expect "scan: release builds" 0 "$rc"
capture smith build
expect "scan: debug builds" 0 "$rc"
for profile in release debug; do
    capture "build/$profile/scan"
    expect "scan: $profile, Enzyme equals the hand-written backward to 1e-12" 0 "$rc"
    expect_contains "scan: $profile reports the difference" "$out" "worst relative difference"
done

# 4. A smith that runs an external clang has no Enzyme: the build refuses an autodiff program and says why.
cd "$work/toy" || exit 1
rm -rf .smith build
capture env SMITH_CLANG=/usr/bin/clang smith build
expect "refusal: a smith with an external clang fails" 2 "$rc"
expect_contains "refusal: ... saying it needs Enzyme" "$err" "smith::autodiff needs a smith with Enzyme linked in"

echo "$failures failed"
exit "$failures"
