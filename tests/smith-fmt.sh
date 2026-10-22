#!/bin/sh
# `smith fmt` on .smith files (issue #25): clang-format reads a .smith file as C, with `::` paths and the `module` and
# `import` lines at file scope (patches/clang-format-smith.patch). Usage:
#   sh tests/smith-fmt.sh path/to/smith     run the tests
#   UPDATE=1 sh tests/smith-fmt.sh smith    write the corpus's expected output (read the diff)
# The tests:
#   - the .smith files of the #11 corpus (tests/smith-lang/cases/*/src) format to tests/smith-fmt/corpus.expected;
#   - every .c file of smith, and of the test beds when they are there (game, learner, checkers; read, never written),
#     formats identically as itself and as a .smith file (the text goes to standard input, --assume-filename names it);
#   - the cases of the issue: a C name that is a C++ keyword (`throw`) and a statement that starts with a type named
#     `import`, and `import <sys/wait.h>;`, `import a::b as x;`, `module a::b;` kept whole.
# A smith whose clang-format has no patch (a stage0 runs Homebrew's) skips itself.
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here" || exit 1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
failures=0
total=0

fail() {
    failures=$((failures + 1))
    echo "FAIL  $1"
}

# Formats standard input as a file of this name, in this folder's style.
format() { # name
    "$smith" fmt --assume-filename="$1"
}

# A statement whose type is named import is a declaration, and indents: only a patched clang-format does that.
printf 'u32 f(void) {\nimport value = 1;\n}\n' >"$work/probe.smith"
if [ "$(format "$here/probe.smith" <"$work/probe.smith" 2>/dev/null | sed -n 2p)" != "    import value = 1;" ]; then
    echo "skip  smith-fmt: this smith's clang-format has no patch for .smith files"
    exit 0
fi

expect() { # name, expected text, actual text
    total=$((total + 1))
    if [ "$2" = "$3" ]; then
        echo "ok    $1"
    else
        fail "$1"
        printf '%s\n' "$2" >"$work/expected"
        printf '%s\n' "$3" >"$work/actual"
        diff "$work/expected" "$work/actual" | head -20
    fi
}

# --- the corpus ----------------------------------------------------------------------------------------------------
corpus=tests/smith-lang/cases
: >"$work/corpus"
for file in $(find "$corpus" -name '*.smith' -path '*/src/*' | LC_ALL=C sort); do
    echo "==> ${file#"$corpus"/} <==" >>"$work/corpus"
    # The cases are in .clang-format-ignore (they are inputs written as the case needs), so the text is formatted under
    # another name, in the style of this folder.
    "$smith" fmt --style="file:$here/.clang-format" --assume-filename="$work/case.smith" <"$file" >>"$work/corpus" 2>&1
done
if [ "${UPDATE:-}" = 1 ]; then
    mkdir -p tests/smith-fmt
    cp "$work/corpus" tests/smith-fmt/corpus.expected
    echo "wrote tests/smith-fmt/corpus.expected"
else
    total=$((total + 1))
    if cmp -s tests/smith-fmt/corpus.expected "$work/corpus"; then
        echo "ok    corpus: $(grep -c '^==> ' "$work/corpus") .smith files format to tests/smith-fmt/corpus.expected"
    else
        fail "corpus: tests/smith-fmt/corpus.expected"
        diff tests/smith-fmt/corpus.expected "$work/corpus" | head -30
    fi
fi

# --- the same as C -------------------------------------------------------------------------------------------------
same=0
different=0
own=0 # smith's own files among them: in an agent's worktree, under .claude/worktrees, they are not to be skipped
for root in "$here" "$HOME/Documents/game" "$HOME/Documents/virtual-cell/learner" "$HOME/Documents/checkers"; do
    [ -d "$root" ] || continue
    # What the root's .clang-format-ignore names is never formatted (clang-format skips it under its own name, not under
    # the .smith one), so it is not compared: each pattern, `**` read as `*`, becomes a path find leaves out.
    set --
    if [ -f "$root/.clang-format-ignore" ]; then
        while IFS= read -r pattern; do
            case $pattern in '' | '#'*) continue ;; esac
            set -- "$@" -not -path "$root/$(printf '%s' "$pattern" | sed 's/\*\*/*/g')"
        done <"$root/.clang-format-ignore"
    fi
    for file in $(find "$root" -name '*.c' -not -path '*/.smith/*' -not -path '*/build/*' -not -path "$root/.claude/*" \
        -not -path '*/.git/*' -not -path '*/node_modules/*' -not -path '*/tree-sitter/src/*' -not -path '*/expect/*' "$@" |
        LC_ALL=C sort); do
        as_c=$(format "$file" <"$file" 2>&1)
        as_smith=$(format "${file%.c}.smith" <"$file" 2>&1)
        [ "$root" = "$here" ] && own=$((own + 1))
        if [ "$as_c" = "$as_smith" ]; then
            same=$((same + 1))
        else
            different=$((different + 1))
            echo "      differs as .smith: $file"
        fi
    done
done
expect "the C files of smith itself are among those checked, wherever the checkout is" yes "$([ "$own" -gt 20 ] && echo yes || echo "no ($own)")"
total=$((total + 1))
if [ $different = 0 ] && [ $same -gt 0 ]; then
    echo "ok    $same C files format the same as .c and as .smith"
else
    fail "C files formatted differently as .smith: $different of $((same + different))"
fi

# --- the cases of the issue ----------------------------------------------------------------------------------------
as_smith() { format "$here/x.smith"; }
expect "a C name that is a C++ keyword stays C" "u32 f(u32 throw, u32 reach) {
    return throw * reach;
}" "$(printf 'u32 f(u32 throw, u32 reach) {\nreturn throw*reach;\n}\n' | as_smith)"
expect "a statement that starts with a type named import is indented" "u32 f(void) {
    import value = c.x + c.y;
    return 1;
}" "$(printf 'u32 f(void) {\nimport value = c.x + c.y;\nreturn 1;\n}\n' | as_smith)"
expect "import <h> is kept whole" "import <sys/wait.h>;" "$(printf 'import   <sys/wait.h> ;\n' | as_smith)"
expect "module and import lines" "module a::b;
import a::b as x;
import lib::text;
import \"local.h\";" "$(printf 'module   a::b ;\nimport a::b  as x;\nimport lib::text ;\nimport  \"local.h\" ;\n' | as_smith)"
expect "paths with ::" "x::Pair make(x::T *p) {
    struct x::Pair q = {1, 2};
    return c::EOF + x::open(p);
}" "$(printf 'x::Pair make(x::T*p){\nstruct x::Pair q={1,2};\nreturn c::EOF+x::open(p);}\n' | as_smith)"
expect "export lines" "export u32 counter = 7;
export u64 checksum(const u8 *data, usize size);" "$(printf 'export   u32 counter=7;\nexport u64 checksum(const u8*data,usize size);\n' | as_smith)"
expect "a name import or module in a function is a name" "void f(void) {
    module value;
    import *p;
}" "$(printf 'void f(void){\nmodule value;\nimport*p;\n}\n' | as_smith)"
expect ".c files still read import as a name" "u32 f(void) {
    import value = 1;
    return value;
}" "$(printf 'u32 f(void) {\nimport value = 1;\nreturn value;\n}\n' | format "$here/x.c")"

# A file's own header sorts first (patches/smith-format-source-extension.patch): `#include "foo.h"` in foo.smith, as in foo.c.
expect "a .smith file's own header sorts first, as in a .c file" '#include "foo.h"
#include <stdlib.h>' "$(printf '#include <stdlib.h>\n#include "foo.h"\n' | format "$here/foo.smith")"
expect "... and the same in the .c file" "$(printf '#include <stdlib.h>\n#include "foo.h"\n' | format "$here/foo.smith")" \
    "$(printf '#include <stdlib.h>\n#include "foo.h"\n' | format "$here/foo.c")"

# --- `smith fmt` with no arguments --------------------------------------------------------------------------------------
# In a git project `smith fmt` formats every .smith file git sees (no build.c lists them).
project=$work/project
mkdir -p "$project/src/a" "$project/tests"
for f in main.c primes.c primes.h; do sed 's/@NAME@/demo/g' "templates/new/$f" >"$project/$f"; done
sed 's/@NAME@/demo/g' templates/new/test_primes.c >"$project/tests/test_primes.c"
cp templates/defaults/clang-format "$project/.clang-format"
git -C "$project" init -q
printf 'module   a::b ;\nimport <sys/wait.h> ;\nexport u32 f(x::T*p){\nimport value=1;\nreturn c::EOF;}\n' >"$project/src/a/b.smith"
total=$((total + 1))
if (cd "$project" && "$smith" fmt --dry-run --Werror src/a/b.smith >/dev/null 2>&1); then
    fail "a project's check-format: the unformatted .smith file was not reported"
else
    echo "ok    the project's .smith file is reported unformatted by clang-format"
fi
(cd "$project" && "$smith" fmt >"$work/fmt.out" 2>&1)
expect "smith fmt with no arguments formats the project's .smith files" "module a::b;
import <sys/wait.h>;
export u32 f(x::T *p) {
    import value = 1;
    return c::EOF;
}" "$(cat "$project/src/a/b.smith")"

echo "smith-fmt: $((total - failures)) of $total passed"
[ $failures = 0 ]
