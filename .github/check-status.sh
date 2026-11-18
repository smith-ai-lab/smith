#!/bin/sh
# Checks that every page of knowledge/language/ except index.md says how much of
# it the bootstrap compiler builds: a `status` of built, partial, or planned in
# its front matter, and, beside a partial one, a `built` list: the key alone on
# its line, then one built thing to a line, each line starting with `  - `.
# Usage: check-status.sh [<folder>]
set -eu

folder=${1:-knowledge/language}
status=0
for file in "$folder"/*.md; do
  [ "$(basename "$file")" = index.md ] && continue
  # The front matter is what lies between the first two lines of three dashes.
  front=$(awk '/^---$/ { n++; next } n == 1 { print } n == 2 { exit }' "$file")
  state=$(printf '%s\n' "$front" | sed -n 's/^status: *//p')
  # What follows `built:` on its own line, and the lines that are its items.
  inline=$(printf '%s\n' "$front" | sed -n 's/^built: *//p')
  items=$(printf '%s\n' "$front" | awk '/^built:/ { in_list = 1; next } /^[^ ]/ { in_list = 0 } in_list && /^  - / { n++ } END { print n + 0 }')
  if [ -n "$inline" ]; then
    echo "$file: built is a list, one built thing to a line, and not a line of text"
    status=1
  fi
  case "$state" in
    built | planned) ;;
    partial)
      if [ "$items" -eq 0 ]; then
        echo "$file: a partial page needs a built list"
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
