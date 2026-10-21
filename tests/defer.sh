#!/bin/sh
# Tests of scripts/defer_init.c, which moves a Mach-O object's static initializers out of the section dyld runs at load
# (scripts/llvm.sh uses it on the objects of clang-format, clang-tidy and clangd). macOS only. Usage:
# sh tests/defer.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
if [ "$(uname -s)" != Darwin ]; then
    echo "skip  defer: Mach-O objects only"
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
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

"$smith" cc -std=gnu23 -Wall -Wextra -Wpedantic -Werror "$here/scripts/defer_init.c" -o "$work/defer_init" || exit 1
tool=$work/defer_init

# Two objects with an initializer each, and a main that runs the section of one of them by hand.
cat >"$work/a.c" <<'EOF'
#include <stdio.h>
__attribute__((constructor)) static void init_a(void) { puts("init a"); }
EOF
cat >"$work/b.c" <<'EOF'
#include <stdio.h>
__attribute__((constructor)) static void init_b(void) { puts("init b"); }
EOF
cat >"$work/main.c" <<'EOF'
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#include <stdio.h>
int main(void) {
    puts("main");
    unsigned long size = 0;
    void (**table)(void) = (void (**)(void))getsectiondata(&_mh_execute_header, "__DATA", "__smith_demo", &size);
    printf("section %s, %lu entries\n", table != nullptr ? "found" : "missing", size / sizeof table[0]);
    for (unsigned long i = 0; table != nullptr && i < size / sizeof table[0]; i++)
        table[i]();
    return 0;
}
EOF
for f in a b main; do "$smith" cc -std=gnu23 -c "$work/$f.c" -o "$work/$f.o" || exit 1; done

"$smith" cc "$work/main.o" "$work/a.o" "$work/b.o" -o "$work/plain" || exit 1
expect "before: both initializers run at load, before main" "$(printf 'init a\ninit b\nmain\nsection missing, 0 entries')" "$("$work/plain")"

out=$("$tool" "$work/a.o" "$work/a_deferred.o" demo)
expect "the tool says what it did" "$work/a.o: 1 initializer section(s) deferred as __smith_demo" "$out"
"$tool" "$work/b.o" "$work/b_deferred.o" demo >/dev/null
"$tool" "$work/main.o" "$work/main_same.o" demo >/dev/null
expect "an object with no initializer is unchanged" "$(shasum "$work/main.o" | cut -d' ' -f1)" "$(shasum "$work/main_same.o" | cut -d' ' -f1)"

"$smith" cc "$work/main.o" "$work/a_deferred.o" "$work/b_deferred.o" -Wl,-dead_strip -o "$work/deferred" || exit 1
expect "after: nothing runs at load, and the section holds both, kept by -dead_strip, to run by hand" \
    "$(printf 'main\nsection found, 2 entries\ninit a\ninit b')" "$("$work/deferred")"
expect "the section is plain data in the object" yes \
    "$(otool -l "$work/a_deferred.o" | grep -q 'sectname __smith_demo' && echo yes || echo no)"
expect "no __mod_init_func is left in the object" no \
    "$(otool -l "$work/a_deferred.o" | grep -q 'sectname __mod_init_func' && echo yes || echo no)"

"$tool" "$work/missing.o" "$work/x.o" demo >/dev/null 2>&1
expect "a missing input fails" 1 "$?"
"$tool" "$work/a.o" >/dev/null 2>&1
expect "a wrong usage is 2" 2 "$?"
printf 'not an object' >"$work/text.o"
"$tool" "$work/text.o" "$work/x.o" demo >/dev/null 2>&1
expect "a file that is no Mach-O object is refused" 1 "$?"

echo
echo "$total defer checks, $failures failed"
[ "$failures" -eq 0 ]
