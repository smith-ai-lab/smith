#!/bin/sh
# <smith/http.h>'s library compiled under the warnings of a new project. Usage: sh tests/http.sh path/to/smith
# The behavior (a scripted network, the fake io and a real one, no internet) is tested by tests/http_test.smith.
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
cc=${CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
failures=0
total=0
rc=0

capture() {
    "$@" >/dev/null 2>"$work/err"
    rc=$?
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

for std in gnu23 gnu2y; do
    capture $cc -std=$std $warn -I "$here/include" -c "$here/lib/http.c" -o "$work/http.o"
    expect "library: compiles as -std=$std with the new project's warnings" 0 "$rc"
done
capture $cc -std=gnu23 $warn -O2 -DNDEBUG -I "$here/include" -c "$here/lib/http.c" -o "$work/http.o"
expect "library: compiles with NDEBUG and the same warnings" 0 "$rc"

echo "$total http checks, $failures failed"
[ "$failures" -eq 0 ]
