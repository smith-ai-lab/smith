#!/bin/sh
# Fetches every v* tag from origin and prints, one on each line, the refspec
# that pushes it to GitLab under its own name.
# Usage: mirror-tags.sh
set -eu

# The `v` is part of the pattern on both sides, so the tag keeps it.
git fetch --quiet origin '+refs/tags/v*:refs/mirror/tags/v*'
for tag in $(git for-each-ref --format='%(refname:strip=3)' refs/mirror/tags); do
  echo "refs/mirror/tags/$tag:refs/tags/$tag"
done
