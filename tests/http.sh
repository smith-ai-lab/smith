#!/bin/sh
# Tests of <smith/http.h> on a scripted network (no internet). Usage: sh tests/http.sh path/to/smith
# The checks in tests/http.c run on the fake io and on a real io with a scripted network, sanitized and optimized.
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
cc=${CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
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

warn="-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wimplicit-fallthrough -Wmissing-prototypes -Wstrict-prototypes -Wvla -Werror"
sources="$here/tests/http.c $here/lib/http.c $here/bootstrap/lib/smith/io.c $here/bootstrap/lib/smith/arena.c $here/bootstrap/lib/smith/random.c $here/bootstrap/lib/smith/contract.c $here/lib/trace.c"
mkdir "$work/run" "$work/bin"
cd "$work/run" || exit 1

$cc -std=gnu23 $warn -I "$here/include" -O0 -g -fsanitize=address,undefined $sources -o "$work/bin/asan" || exit 1
capture "$work/bin/asan"
expect "checks: pass at -O0 under ASan and UBSan" "0 http: all checks passed" "$rc $out"
expect "checks: ... and nothing was printed to stderr" "" "$err"
$cc -std=gnu23 $warn -I "$here/include" -O2 $sources -o "$work/bin/fast" || exit 1
capture "$work/bin/fast"
expect "checks: pass at -O2" "0 http: all checks passed" "$rc $out"

for std in gnu23 gnu2y; do
    capture $cc -std=$std $warn -I "$here/include" -c "$here/lib/http.c" -o "$work/http.o"
    expect "library: compiles as -std=$std with the new project's warnings" 0 "$rc"
done
capture $cc -std=gnu23 $warn -O2 -DNDEBUG -I "$here/include" -c "$here/lib/http.c" -o "$work/http.o"
expect "library: compiles with NDEBUG and the same warnings" 0 "$rc"

echo "$total http checks, $failures failed"
[ "$failures" -eq 0 ]
