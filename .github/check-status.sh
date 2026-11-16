#!/bin/sh
# Checks that every page of knowledge/language/ except index.md says how much of
# it the bootstrap compiler builds: a `status` of built, partial, or planned in
# its front matter, and a `built` line beside a partial one.
# Usage: check-status.sh [<folder>]
set -eu

folder=${1:-knowledge/language}
status=0
for file in "$folder"/*.md; do
  [ "$(basename "$file")" = index.md ] && continue
  # The front matter is what lies between the first two lines of three dashes.
  front=$(awk '/^---$/ { n++; next } n == 1 { print } n == 2 { exit }' "$file")
  state=$(printf '%s\n' "$front" | sed -n 's/^status: *//p')
  built=$(printf '%s\n' "$front" | sed -n 's/^built: *//p')
  case "$state" in
    built | planned) ;;
    partial)
      if [ -z "$built" ]; then
        echo "$file: a partial page needs a built line"
        status=1
      fi
      ;;
    "")
      echo "$file: no status in the front matter"
      status=1
      ;;
    *)
      echo "$file: the status is '$state', not built, partial, or planned"
      status=1
      ;;
  esac
done
exit "$status"
