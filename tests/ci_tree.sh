#!/bin/sh
# Tests for scripts/ci-tree.sh (issue #110) with no network: the tree of a scratch git repository, and both branches of the
# skip rule against a stub gh that answers from a file. Usage: sh tests/ci_tree.sh [path/to/smith] (the argument is unused)
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
script=$here/scripts/ci-tree.sh
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
failures=0
check() { # check <name> <expected> <actual>
    if [ "$2" != "$3" ]; then
        failures=$((failures + 1))
        printf 'FAIL %s\n--- expected\n%s\n--- actual\n%s\n' "$1" "$2" "$3"
    fi
}

# a stub gh: prints $STUB_ANSWER when asked for the artifact named in $STUB_NAME, nothing otherwise; STUB_FAIL makes it fail
mkdir "$work/bin"
cat >"$work/bin/gh" <<'STUB'
#!/bin/sh
[ -n "${STUB_FAIL:-}" ] && exit 1
case $2 in
*"artifacts?name=$STUB_NAME") printf '%s' "${STUB_ANSWER:-}" ;;
esac
STUB
chmod +x "$work/bin/gh"
export PATH="$work/bin:$PATH" GITHUB_REPOSITORY=o/r

repo=$work/repo
mkdir "$repo" && cd "$repo" || exit 1
git init -q . && git config user.email t@t && git config user.name t
echo a >f && git add f && git commit -qm one
tree1=$(sh "$script" tree)
check tree-is-the-tree "$(git rev-parse 'HEAD^{tree}')" "$tree1"
# a rebase or amend that keeps the files keeps the tree; a changed file changes it
git commit -q --amend -m reworded
check same-files-same-tree "$tree1" "$(sh "$script" tree)"
echo b >f && git commit -qam two
tree2=$(sh "$script" tree)
[ "$tree1" != "$tree2" ] || check different-tree differ same

export STUB_NAME=tree-$tree1 STUB_ANSWER=4242
check found-checked yes "$(sh "$script" checked "$tree1")"
check push-same-tree-skips "skip 4242" "$(sh "$script" decide push "$tree1")"
check pr-same-tree-never-skips run "$(sh "$script" decide pull_request "$tree1")"
check different-tree-runs run "$(sh "$script" decide push "$tree2")"
check different-tree-not-checked no "$(sh "$script" checked "$tree2")"
STUB_ANSWER=
check expired-or-missing-runs run "$(sh "$script" decide push "$tree1")"
check missing-not-checked no "$(sh "$script" checked "$tree1")"
STUB_ANSWER=4242
export STUB_FAIL=1
check gh-fails-runs run "$(sh "$script" decide push "$tree1")"
unset STUB_FAIL

GITHUB_RUN_ID=77 sh "$script" record "$tree1" "$work/rec"
check record "tree $tree1
run 77" "$(cat "$work/rec/tree.txt")"
check usage 2 "$(sh "$script" nonsense 2>/dev/null; echo $?)"

if [ "$failures" -ne 0 ]; then
    echo "ci_tree: $failures failed"
    exit 1
fi
echo "ci_tree: ok"
