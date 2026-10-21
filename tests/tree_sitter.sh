#!/bin/sh
# Tests of the tree-sitter grammar for .smith (issue #24, tree-sitter/), without the tree-sitter CLI or Node: the
# committed parser tree-sitter/src/parser.c is compiled by smith, with the tree-sitter runtime pinned by hash, into
# tests/tree_sitter.c, which
#   - runs the corpus tests of tree-sitter/test/corpus (one per construct),
#   - parses the .smith samples of tests/tree_sitter_cases, and the #11 corpus (tests/smith-lang/cases) once it
#     exists, with no ERROR or MISSING node (and the ones that are wrong on purpose with one),
#   - parses every .c and .h file of smith (the C++ of shim/ and the generated parser excepted) the same way.
# Usage: sh tests/tree_sitter.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
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
        printf '  expected: %s\n' "$2"
        printf '  actual:   %s\n' "$3"
        printf '%s\n%s\n' "$out" "$err"
    fi
}

mkdir "$work/bin" && ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" && ln -s "$smith" "$work/bin/smith-c++"
PATH="$work/bin:$PATH"
export XDG_CACHE_HOME=$work/xdg-cache # the module cache of the tests, never the real one

# The tree-sitter runtime, pinned by the hash of its archive (v0.27.1, which reads parsers of ABI 15, the one
# tree-sitter/src/parser.c is generated with). It is fetched into a cache of its own, so that it is downloaded once.
runtime_url=https://github.com/tree-sitter/tree-sitter/archive/refs/tags/v0.27.1.tar.gz
runtime_hash=sha256:982cd3d4d9eb7be18c243240a622423fd2e4ecb4bec2cd98832c2f3fa1f0f333
cache=${SMITH_TREE_SITTER_CACHE:-$HOME/.cache/smith-tests}
mkdir -p "$cache"
fetched=$(XDG_CACHE_HOME=$cache smith fetch "$runtime_url" 2>&1)
case $fetched in
*"$runtime_hash"*) runtime=$(printf '%s\n' "$fetched" | sed -n 's/^smith: .* is in //p')/lib ;;
*)
    echo "SKIP  the tree-sitter runtime is not available (no network?): $fetched"
    exit 0
    ;;
esac

# The driver, the runtime and the parser, compiled by smith.
driver=$work/driver
capture smith cc -std=gnu23 -O1 -g -Wall -Wextra -Wpedantic -Werror -I "$runtime/include" -c "$here/tests/tree_sitter.c" -o "$work/driver.o"
expect "the driver compiles under -Werror" 0 "$rc"
capture smith cc -std=gnu11 -O2 -w -D_POSIX_C_SOURCE=200112L -D_DEFAULT_SOURCE -I "$runtime/include" -I "$runtime/src" -c "$runtime/src/lib.c" -o "$work/runtime.o"
expect "the tree-sitter runtime compiles" 0 "$rc"
capture smith cc -std=gnu11 -O1 -w -I "$here/tree-sitter/src" -c "$here/tree-sitter/src/parser.c" -o "$work/parser.o"
expect "the committed parser.c compiles" 0 "$rc"
capture smith cc "$work/driver.o" "$work/runtime.o" "$work/parser.o" -o "$driver"
expect "the driver links" 0 "$rc"
[ "$rc" -eq 0 ] || { echo "$total tests, $failures failed"; exit 1; }

# The corpus: one test per construct, and the count of the tests in the files, so that none is skipped.
corpus_files=$(ls "$here"/tree-sitter/test/corpus/*.txt)
written=$(cat $corpus_files | grep -c '^=\{3,\}$')
capture "$driver" --corpus $corpus_files
expect "corpus: every test parses to its tree" "$((written / 2)) corpus tests, 0 failed" "$(printf '%s\n' "$out" | tail -1)"
for file in $corpus_files; do
    printf '      %s: %s tests\n' "$(basename "$file")" "$(grep -c '^=\{3,\}$' "$file" | awk '{print $1 / 2}')"
done

# The samples of tests/tree_sitter_cases: each parses without an ERROR or MISSING node, but error-*.smith.
samples=$(ls "$here"/tests/tree_sitter_cases/*.smith | grep -v '/error-')
count=$(printf '%s\n' "$samples" | wc -l | tr -d ' ')
capture "$driver" $samples
expect "samples: $count .smith files parse with no ERROR or MISSING node" "" "$(printf '%s\n' "$out" | grep -v '^ok')"
errors=$(ls "$here"/tests/tree_sitter_cases/error-*.smith)
capture "$driver" --expect-error $errors
expect "samples: text that is wrong on purpose has an ERROR or MISSING node" "" "$(printf '%s\n' "$out" | grep -v '^ok')"

# The #11 corpus, when it exists (it arrives with #11). Its error cases are wrong in what the modules mean, not in
# their text, so they parse (`module ;` is C: an empty declaration of a type called module); only the import with no
# semicolon, and a keyword where a field's name goes after `short` (error-keyword-module), are expected to fail. The
# grammar is tree-sitter-c's, which has neither C23's `auto` as a type nor C2y's `_Countof`: the three cases that use
# them are left out until the grammar learns them.
lang=${SMITH_LANG_CASES:-$here/tests/smith-lang/cases}
bad='/error-import-eof/src/main\.smith$|/error-keyword-module/src/shapes\.smith$'
gap='/session-two-modules/src/main\.smith$|/session-missing-module/src/main\.smith$|/std-default-c2y/src/main\.smith$'
if [ -d "$lang" ]; then
    find "$lang" -name '*.smith' | sort >"$work/lang.txt"
    grep -E "$bad" "$work/lang.txt" >"$work/lang-bad.txt"
    grep -v -E "$bad|$gap" "$work/lang.txt" >"$work/lang-good.txt"
    count=$(wc -l <"$work/lang-good.txt" | tr -d ' ')
    capture xargs "$driver" <"$work/lang-good.txt"
    expect "smith-lang: $count .smith files of the #11 corpus parse with no ERROR or MISSING node" "" "$(printf '%s\n' "$out" | grep -v '^ok')"
    capture xargs "$driver" --expect-error <"$work/lang-bad.txt"
    expect "smith-lang: the case wrong in its text has an ERROR or MISSING node" "" "$(printf '%s\n' "$out" | grep -v '^ok')"
else
    echo "SKIP  the #11 corpus ($lang) does not exist yet; set SMITH_LANG_CASES to check against a copy"
fi

# smith's own C: plain C stays C (the expected outputs of the #11 corpus are generated and not always C), with the C23 and clang spellings smith uses.
cd "$here" || exit 1
find . \( -path ./build -o -path ./shim -o -path ./tree-sitter -o -path ./.git -o -path ./.smith -o -path ./.cache -o -path ./.claude -o -path './tests/smith-lang/cases/*/expect' \) -prune \
    -o \( -name '*.c' -o -name '*.h' \) -print | sort >"$work/c-files.txt"
count=$(wc -l <"$work/c-files.txt" | tr -d ' ')
capture xargs "$driver" <"$work/c-files.txt"
expect "smith's own C: $count .c and .h files parse with no ERROR or MISSING node" "" "$(printf '%s\n' "$out" | grep -v '^ok')"

echo "$total tests, $failures failed"
[ "$failures" -eq 0 ]
