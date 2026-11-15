#!/bin/sh
# Fails if any commit between two revisions does not follow Conventional Commits.
# Usage: check-commits.sh <base> <head>
set -eu

pattern='^(feat|fix|docs|refactor|test|perf|build|ci|chore|revert)(\([a-z0-9._/-]+\))?!?: [^ ].*[^.]$'
status=0

for commit in $(git rev-list "$1..$2"); do
  subject=$(git log -1 --format=%s "$commit")
  if ! printf '%s\n' "$subject" | grep -Eq "$pattern"; then
    echo "not a conventional commit: $(git log -1 --format=%h "$commit") $subject"
    status=1
  fi
done

if [ "$status" -ne 0 ]; then
  echo
  echo "Expected: <type>(<scope>): <summary>"
  echo "Types: feat fix docs refactor test perf build ci chore revert"
fi
exit "$status"
