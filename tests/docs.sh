#!/bin/sh
# Tests for `smith docs`: every rule on the small repositories of tests/docs/fixtures (each breaks one rule; its
# expect.txt is the exact output, stderr then stdout, and the check exits 1 exactly when there are problems), the
# depfile, the options, and the types an index declares. They need no compiler
# except for the tests of a build.c.
# Usage: sh tests/docs.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
mkdir "$work/bin" "$work/home"
ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc"
PATH=$work/bin:$PATH
export HOME=$work/home
export XDG_CACHE_HOME=$work/xdg-cache # the module and package caches of the tests, never the real ones

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

expect_contains() { # name, text, needle
    total=$((total + 1))
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

# 1. The fixtures: copied (the links in them stay links), checked, and the output compared whole.
mkdir "$work/fixtures"
cp -R "$here/tests/docs/fixtures/." "$work/fixtures/"
cd "$work/fixtures" || exit 1
for fixture in */; do
    fixture=${fixture%/}
    args=
    [ -f "$fixture/args" ] && args=$(cat "$fixture/args")
    # shellcheck disable=SC2086
    smith docs $args "$fixture" >"$work/actual" 2>&1
    rc=$?
    expect "fixture $fixture: output" "$(cat "$fixture/expect.txt")" "$(cat "$work/actual")"
    want=1
    case $(tail -n 1 "$fixture/expect.txt") in *" 0 problems") want=0 ;; esac
    expect "fixture $fixture: exit status" "$want" "$rc"
done

# 2. The depfile names every file read or found, once, escaped as a compiler's -MF writes it.
# shellcheck disable=SC2046
smith docs --depfile "$work/pass.d" --target docs.stamp $(cat pass/args) pass >/dev/null 2>&1
expect "depfile: the documents, the skills and what the links point at" "$(cat pass/expect.d)" "$(cat "$work/pass.d")"
capture smith docs --depfile "$work/bad.d" --target docs.stamp $(cat links/args 2>/dev/null) links
expect "depfile: written when there are problems too" yes "$([ -f "$work/bad.d" ] && echo yes || echo no)"
expect "depfile: a link's target that is not there is not named" 0 "$(grep -c 'gone.md' "$work/bad.d")"

# 3. The options.
capture smith docs --depfile "$work/x.d" pass
expect "options: a depfile needs a target" 2 "$rc"
capture smith docs --target stamp pass
expect "options: a target needs a depfile" 2 "$rc"
capture smith docs --type
expect "options: --type needs a name" 2 "$rc"
capture smith docs --require dataset pass
expect "options: --require needs a field" 2 "$rc"
expect_contains "options: --require says what it takes" "$err" "--require takes <type>:<field>"
capture smith docs --require dataset: pass
expect "options: --require needs a field after the colon" 2 "$rc"
capture smith docs --require dataset:field= pass
expect "options: --require needs a value after the equals" 2 "$rc"
capture smith docs --bogus pass
expect "options: an unknown option is a usage error" 2 "$rc"
capture smith docs pass links
expect "options: two roots are a usage error" 2 "$rc"
capture smith docs "$work/fixtures/pass" --type contract --type dataset
expect "options: an absolute root, options after it" 0 "$rc"
capture smith docs --type contract pass
expect_contains "options: --type replaces the default types" "$err" "knowledge/data.md: type 'dataset' is not one of contract"
capture smith docs --type contract --type dataset --require dataset:license_class=research-only pass
expect "options: a value list accepts a value in it" 0 "$rc"
capture smith docs --type contract --type dataset --require dataset:license_class=commercial-ok,unknown pass
expect_contains "options: a value list rejects a value outside it" "$err" \
    "knowledge/data.md: type 'dataset' needs license_class: commercial-ok, unknown"

# 4. The verb. With no arguments it checks the current folder, with the types and required fields that
# knowledge/index.md declares in its frontmatter, and the defaults when it declares none.
cd pass || exit 1
capture smith docs
expect_contains "no arguments: checks the current folder" "$out" "smith docs: 2 concepts"
expect_contains "no arguments: the default types apply" "$err" "type 'dataset' is not one of convention, architecture"
cd ..
cd links || exit 1
capture smith docs
expect "no arguments: problems exit 1" 1 "$rc"
cd ..
cd declared || exit 1
capture smith docs
expect "declared types: the index's types and requirements are the project's" 0 "$rc"
capture smith docs --type dataset
expect_contains "declared types: --type replaces the index's types" "$err" "type 'contract' is not one of dataset"
capture smith docs --type contract --type dataset --require contract:owner
expect_contains "declared types: --require replaces the index's requirements" "$err" "type 'contract' needs owner"
cd ..

# A build.c changes nothing: the verb is the check whatever the build declares, and it needs no task.
mkdir verb && cp -R declared/. verb/
cd verb || exit 1
cat >build.c <<'EOF'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep other = {.command = smith_list(b, "true", nullptr)};
    smith_task(b, "other", &other);
    smith_default(b, other.outputs);
}
EOF
capture smith docs
expect "build.c without a docs task: the verb checks" 0 "$rc"
expect_contains "build.c without a docs task: the check ran" "$out" "smith docs: 2 concepts, 0 problems"
printf -- '---\ntype: poem\ntitle: Poem\ndescription: A poem.\n---\n' >knowledge/poem.md
capture smith docs
expect "build.c: a problem is the verb's failure" 1 "$rc"
expect_contains "build.c: the problem is reported" "$err" "knowledge/poem.md: type 'poem' is not one of contract, dataset"
capture smith build docs
expect "smith build docs: refused, it is a verb" 2 "$rc"
expect_contains "smith build docs: points at the verb" "$err" "smith docs"
cd ..

echo
echo "$total docs checks, $failures failed"
[ "$failures" -eq 0 ]
