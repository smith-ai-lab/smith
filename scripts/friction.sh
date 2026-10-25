#!/bin/sh
# The friction sections of merged pull requests, for the lead's review across PRs (AGENTS.md, How to work): each pull
# request's body ends with `## Friction`; this prints that section of every PR merged since a date, newest first.
#   sh scripts/friction.sh 2026-10-24      the sections, each under its PR's number and title
set -u
since=${1:?usage: friction.sh YYYY-MM-DD}
gh pr list --state merged --limit 200 --search "merged:>=$since" --json number,title,body \
    --jq '.[] | select(.body | test("(?m)^## Friction")) |
        "#\(.number) \(.title)\n\(.body | split("## Friction")[1] | split("\n## ")[0] | ltrimstr("\n"))\n"'
