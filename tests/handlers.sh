#!/bin/sh
# Tests of the handlers runtime (issue #239, runtime/handlers.c, <smith/handlers.h>): the dylib's __interpose tuples send
# malloc and its family to the allocator of the calling thread, for the program, a library linked beside it and one loaded
# with dlopen. macOS only (the tuples are dyld's); skipped elsewhere. Usage: sh tests/handlers.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
if [ "$(uname)" != Darwin ]; then
    echo "skipped: the handlers runtime is dyld's __interpose, macOS only"
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
export XDG_CACHE_HOME=$work/xdg-cache XDG_DATA_HOME=$work/xdg-data
failures=0
total=0

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

cd "$work" || exit 1
flags="-std=gnu23 -Wall -Wextra -Wpedantic -Werror -I $here/include"
"$smith" cc $flags -dynamiclib "$here/runtime/handlers.c" -o libsmith_handlers.dylib 2>cc.err
expect "the runtime builds as a dylib under -Wall -Wextra -Wpedantic -Werror" 0 $?
cat cc.err
"$smith" cc $flags -dynamiclib "$here/tests/handlers/foreign.c" -o libforeign.dylib 2>cc.err
expect "the foreign library builds" 0 $?
cat cc.err
"$smith" cc $flags "$here/tests/handlers/main.c" libsmith_handlers.dylib libforeign.dylib -o t_handlers 2>cc.err
expect "the test program builds, linking the runtime and the library" 0 $?
cat cc.err
./t_handlers >out.txt 2>&1
status=$?
total=$((total + $(grep -c '^ok  ' out.txt)))
failures=$((failures + $(grep -c '^FAIL' out.txt)))
grep -v '^ok  ' out.txt
expect "the program passes, from itself, a linked library and a dlopened one" 0 $status
./t_handlers bench
echo "$total handlers checks, $failures failed"
[ "$failures" -eq 0 ]
