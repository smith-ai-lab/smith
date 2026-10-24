#!/bin/sh
# CI's "was this tree already checked" rule (knowledge/ci.md, issue #110). A green Full check records the tree it checked as
# an artifact named tree-<tree-sha>; a push to main whose tree has a live such artifact skips the Full check.
#   ci-tree.sh tree                    the tree sha of HEAD
#   ci-tree.sh run <tree>              the id of the run that checked <tree>, or nothing (gh api; any failure is "nothing")
#   ci-tree.sh checked <tree>          yes or no
#   ci-tree.sh decide <event> <tree>   "skip <run id>" or "run": a pull request event never skips
#   ci-tree.sh record <tree> <folder>  writes <folder>/tree.txt (the tree and this run's id), the artifact's content
set -u
cmd=${1:-}
case $cmd in
tree) git rev-parse 'HEAD^{tree}' ;;
run)
    # fail safe: only a single run id (digits) counts; an error, an empty or a malformed answer is "nothing"
    id=$(gh api "repos/${GITHUB_REPOSITORY:-}/artifacts?name=tree-$2" \
        --jq '[.artifacts[] | select(.expired == false)] | first | .workflow_run.id // empty' 2>/dev/null) || id=
    case $id in
    '' | *[!0-9]*) ;;
    *) printf '%s\n' "$id" ;;
    esac
    ;;
checked)
    if [ -n "$(sh "$0" run "$2")" ]; then echo yes; else echo no; fi
    ;;
decide)
    if [ "$2" = pull_request ]; then
        echo run
        exit 0
    fi
    id=$(sh "$0" run "$3")
    if [ -n "$id" ]; then echo "skip $id"; else echo run; fi
    ;;
record)
    mkdir -p "$3"
    printf 'tree %s\nrun %s\n' "$2" "${GITHUB_RUN_ID:-unknown}" >"$3/tree.txt"
    ;;
*)
    echo "usage: ci-tree.sh tree | run <tree> | checked <tree> | decide <event> <tree> | record <tree> <folder>" >&2
    exit 2
    ;;
esac
