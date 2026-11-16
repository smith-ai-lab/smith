#!/bin/sh
# Prints the jobs of this run whose name begins with the argument, oldest first,
# one a line, for never-started.sh: the kind of machine, what the job ended as,
# and how many steps it ran, separated by tabs. Needs GH_TOKEN, and reads the
# run named by GITHUB_REPOSITORY and GITHUB_RUN_ID.
set -eu

# A request that fails stops the script, so the caller never sees a short list.
jobs=$(gh api --paginate "repos/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID/jobs")
printf '%s\n' "$jobs" |
  jq -rs --arg prefix "$1" '
    [.[].jobs[]] | sort_by(.id)[]
    | select(.name | startswith($prefix))
    | [(.labels[0] // ""), .conclusion, ((.steps // []) | length)]
    | @tsv'
