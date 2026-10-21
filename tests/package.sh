#!/bin/sh
# Tests of package downloads on a scripted network (no internet): redirects, a cross-scheme redirect refused, 404, a
# truncated body, a broken connection, a wrong hash, a TLS failure and a killed fetch's leftovers. The cache and tar are
# real; the network is io's scripted one. Usage: sh tests/package.sh path/to/smith
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

warn="-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wimplicit-fallthrough -Wmissing-prototypes -Wstrict-prototypes -Wvla -Werror -Wno-unused-function"
mkdir -p "$work/src/pkg-1.0/bin" "$work/scratch" "$work/bin"
printf 'hello package\n' >"$work/src/pkg-1.0/hello.txt"
printf '#!/bin/sh\necho tool ran\n' >"$work/src/pkg-1.0/bin/tool"
chmod +x "$work/src/pkg-1.0/bin/tool"
(cd "$work/src" && tar -czf ../pkg.tar.gz pkg-1.0)
cd "$work/scratch" || exit 1

$cc -std=gnu23 $warn -I "$here/include" -O0 -g -fsanitize=address,undefined "$here/tests/package.c" -o "$work/bin/asan" || exit 1
capture "$work/bin/asan" "$work/pkg.tar.gz" "$work/scratch"
expect "checks: pass at -O0 under ASan and UBSan" "0 package: all checks passed" "$rc $out"
expect "checks: ... and nothing was printed to stderr" "" "$err"
$cc -std=gnu23 $warn -I "$here/include" -O2 "$here/tests/package.c" -o "$work/bin/fast" || exit 1
rm -rf "$work/scratch" && mkdir "$work/scratch" && cd "$work/scratch" || exit 1
capture "$work/bin/fast" "$work/pkg.tar.gz" "$work/scratch"
expect "checks: pass at -O2" "0 package: all checks passed" "$rc $out"

echo "$total package checks, $failures failed"
[ "$failures" -eq 0 ]
