#!/bin/sh
# Effects, inferred (`shim/effects_infer.cpp`; issue #207, skill smith-lang-effects): each tests/effects/*.c and *.smith
# is compiled with `smith cc -c` and what it prints (the status, then each error and note, the path left off) is compared
# with the file beside it, `<file>.expected`: a pure function over libc, the chain from a pure function to printf, a
# ceiling that fits and one that does not, a write to a global or a function-local static, a call through a function
# pointer, a declaration with no body, with a promise and without, in C and in a module; the editor shows the same
# error. A smith without clang inside (the stage0), or whose clang lacks the token hook, is skipped.
# Usage: sh tests/effects_infer.sh path/to/smith [--update]
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
update=${2:-}
here=$(pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
export XDG_DATA_HOME=$work/data
export XDG_CACHE_HOME=$work/cache
mkdir "$work/bin" && ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" && ln -s "$smith" "$work/bin/smith-c++"
export PATH=$work/bin:$PATH

failures=0
total=0

if [ "$(smith cc --smith-which -c a.c | head -n 1 | cut -d ' ' -f 1,2)" != "clang in-process" ]; then
    echo "skipped: this smith has no clang inside (the stage0)"
    exit 0
fi
printf 'int main(void) { return 0; }\n' >"$work/probe.smith"
if smith cc --smith-lower "$work/probe.smith" 2>&1 | grep -q "token hook"; then
    echo "skipped: this smith's clang lacks the token hook"
    exit 0
fi

# What a compile of the file prints: the status, then the errors and notes.
shown() {
    (cd "$here/tests/effects" && smith cc -std=gnu2y -I "$here/include" -c "$1" -o /dev/null >"$work/out" 2>&1)
    echo "status $?"
    grep -E '^[^ ]+:[0-9]+:[0-9]+: (error|note|warning):' "$work/out"
}

for file in "$here"/tests/effects/*.c "$here"/tests/effects/*.smith; do
    name=$(basename "$file")
    total=$((total + 1))
    shown "$name" >"$work/actual"
    if [ "$update" = "--update" ]; then
        cp "$work/actual" "$file.expected"
    elif diff "$file.expected" "$work/actual" >"$work/diff" 2>&1; then
        echo "ok    $name"
    else
        failures=$((failures + 1))
        echo "FAIL  $name"
        head -20 "$work/diff"
    fi
done

# The editor: clangd shows the same error, for C and for a module.
for name in effects_chain.c effects_chain.smith; do
    total=$((total + 1))
    if smith lsp --check="$here/tests/effects/$name" 2>&1 | grep -q "smith-effect: \`bad\` is \[\[smith::pure\]\]"; then
        echo "ok    lsp $name"
    else
        failures=$((failures + 1))
        echo "FAIL  lsp $name: smith lsp --check shows no effect error"
    fi
done

echo "$((total - failures)) of $total passed"
[ "$failures" -eq 0 ]
