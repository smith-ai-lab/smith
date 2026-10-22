#!/bin/sh
# smith's defaults where a project has no config file (issue #43, part 1). `smith fmt` and `smith lint` use smith's own
# .clang-format and .clang-tidy (templates/defaults, embedded) for a file the tool would find no config for, looked up as
# the tools do (the file's folder and each above it); a project's file replaces them whole, an explicit --style or
# --config wins, and --dump-config prints the effective one, so `smith fmt --dump-config > .clang-format` makes smith's
# the project's. A scratch tree outside any project, with a data folder of its own.
# Usage: sh tests/defaults.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
export XDG_DATA_HOME=$work/data
export XDG_CACHE_HOME=$work/cache

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

if ! "$smith" fmt --version >/dev/null 2>&1 || ! "$smith" lint --version >/dev/null 2>&1; then
    echo "skipped: no clang-format and clang-tidy"
    exit 0
fi

# ---- fmt
mkdir -p "$work/bare/sub" "$work/own/sub" "$work/mixed"
cd "$work/bare" || exit 1
printf 'int main(void) {\nif (1) {\nreturn 0;\n}\n}\n' >a.c
smith_style=$(sed -n 's/^IndentWidth: *//p' "$here/templates/defaults/clang-format")
expect "fmt: smith's default has an indent of 4" 4 "$smith_style"
expect "fmt: no style file, a file is formatted in smith's style" "    if (1) {" "$("$smith" fmt a.c | sed -n 2p)"
expect "fmt: ... and a file in a folder below" "    if (1) {" "$(cp a.c sub/b.c && "$smith" fmt sub/b.c | sed -n 2p)"
expect "fmt: ... and standard input with --assume-filename" "    if (1) {" \
    "$("$smith" fmt --assume-filename=x.c <a.c | sed -n 2p)"
expect "fmt: --dump-config prints smith's style" "$(sed -n 's/^IndentWidth: *//p' "$here/templates/defaults/clang-format")" \
    "$("$smith" fmt --dump-config | sed -n 's/^IndentWidth: *//p')"
"$smith" fmt --dump-config >dumped.yaml
expect "fmt: ... which round-trips as the project's own" "$("$smith" fmt a.c)" \
    "$(cp dumped.yaml .clang-format && "$smith" fmt a.c)"
rm .clang-format

printf 'BasedOnStyle: LLVM\nIndentWidth: 2\n' >"$work/own/.clang-format"
cp a.c "$work/own/a.c"
cp a.c "$work/own/sub/b.c"
cd "$work/own" || exit 1
expect "fmt: a project's .clang-format wins" "  if (1) {" "$("$smith" fmt a.c | sed -n 2p)"
expect "fmt: ... for a file in a folder below it" "  if (1) {" "$("$smith" fmt sub/b.c | sed -n 2p)"
expect "fmt: ... for standard input" "  if (1) {" "$("$smith" fmt --assume-filename=x.c <a.c | sed -n 2p)"
expect "fmt: --dump-config prints the project's style" "2" "$("$smith" fmt --dump-config | sed -n 's/^IndentWidth: *//p')"
expect "fmt: an explicit --style wins over a project file" "        if (1) {" \
    "$("$smith" fmt --style='{BasedOnStyle: LLVM, IndentWidth: 8}' a.c | sed -n 2p)"
mv .clang-format _clang-format
expect "fmt: _clang-format is found too" "  if (1) {" "$("$smith" fmt a.c | sed -n 2p)"
cd "$work/bare" || exit 1
expect "fmt: an explicit --style wins over smith's" "        if (1) {" \
    "$("$smith" fmt --style='{BasedOnStyle: LLVM, IndentWidth: 8}' a.c | sed -n 2p)"
expect "fmt: --assume-filename looks the style up from that name" "  if (1) {" \
    "$("$smith" fmt --assume-filename="$work/own/x.c" <a.c | sed -n 2p)"

# One command naming files with and without a project style.
cp a.c "$work/mixed/none.c"
mkdir "$work/mixed/styled"
cp a.c "$work/mixed/styled/two.c"
printf 'BasedOnStyle: LLVM\nIndentWidth: 2\n' >"$work/mixed/styled/.clang-format"
cd "$work/mixed" || exit 1
"$smith" fmt -i none.c styled/two.c
expect "fmt: files with and without a style, together: smith's for the one" "    if (1) {" "$(sed -n 2p none.c)"
expect "fmt: ... the project's for the other" "  if (1) {" "$(sed -n 2p styled/two.c)"

# The project verb, in a tree with no style file: it formats with smith's.
mkdir "$work/verb"
cd "$work/verb" || exit 1
git init -q . 2>/dev/null
cp "$work/bare/a.c" .
git add a.c 2>/dev/null
"$smith" fmt >/dev/null 2>&1
expect "fmt: the project verb with no style file formats in smith's" "    if (1) {" "$(sed -n 2p a.c)"

# ---- lint
mkdir -p "$work/tidy/sub" "$work/tidyown"
printf 'int BadName(void) {\n    return 0;\n}\n' >"$work/tidy/a.c"
cp "$work/tidy/a.c" "$work/tidy/sub/b.c"
cp "$work/tidy/a.c" "$work/tidyown/a.c"
printf 'Checks: "-*,misc-no-recursion"\n' >"$work/tidyown/.clang-tidy"
lint() { "$smith" lint --quiet "$@" -- -std=gnu2y 2>&1; }
cd "$work/tidy" || exit 1
lint a.c >"$work/out"
rc=$?
expect "lint: no .clang-tidy, smith's rules find a bad name" 1 "$(grep -c 'BadName' "$work/out" | sed 's/^ *//' | sed 's/^[1-9].*/1/')"
"$smith" lint --quiet a.c -- -std=gnu2y >/dev/null 2>&1
expect "lint: ... and fail" 1 "$?"
"$smith" lint --quiet sub/b.c -- -std=gnu2y >/dev/null 2>&1
expect "lint: ... in a folder below too" 1 "$?"
expect "lint: --dump-config prints smith's config" "$(sed -n 's/^HeaderFilterRegex: //p' "$here/templates/defaults/clang-tidy")" \
    "$("$smith" lint --dump-config | sed -n 's/^HeaderFilterRegex: //p')"
"$smith" lint --quiet --config='{Checks: "-*,misc-no-recursion"}' a.c -- -std=gnu2y >/dev/null 2>&1
expect "lint: an explicit --config wins over smith's" 0 "$?"
printf 'Checks: "-*,misc-no-recursion"\n' >"$work/cf.yaml"
"$smith" lint --quiet --config-file="$work/cf.yaml" a.c -- -std=gnu2y >/dev/null 2>&1
expect "lint: an explicit --config-file wins over smith's" 0 "$?"
cd "$work/tidyown" || exit 1
"$smith" lint --quiet a.c -- -std=gnu2y >/dev/null 2>&1
expect "lint: a project's .clang-tidy wins, whole" 0 "$?"
expect "lint: --dump-config prints the project's config" 1 \
    "$("$smith" lint --dump-config | grep -c "^Checks:.*misc-no-recursion")"

echo "$((total - failures)) of $total passed"
[ "$failures" -eq 0 ]
