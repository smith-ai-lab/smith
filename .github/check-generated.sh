#!/bin/sh
# Says which cases the compiler of a change builds to other bytes than the
# compiler of `main` does, by target, and fails when there are any and the
# description of the change does not say it changes generated code.
# Usage: check-generated.sh <main hashes> <change hashes> <description>
# The first two are what `hash-cases.sh` prints for each compiler, and the third
# is the file holding the description of the pull request. A case that only one
# of them builds is not compared: a case a change adds, or one it makes build,
# changes no executable that was there. The description says it with a line
# that is `Changes generated code.` and nothing else.
# The summary is printed as Markdown, to be added to the summary of the run.
set -eu

if [ "$#" -ne 3 ]; then
  echo "usage: check-generated.sh <main hashes> <change hashes> <description>" >&2
  exit 2
fi

differ=$(awk '
  NR == FNR { was[$1 " " $2] = $3; next }
  ($1 " " $2) in was && was[$1 " " $2] != $3 { print $1 " " $2 }
' "$1" "$2" | sort)

if [ -z "$differ" ]; then
  echo "No case builds to other bytes than it does on main, for any target."
  exit 0
fi

echo "These cases build to other bytes than they do on main:"
echo
echo "$differ" | awk '
  { cases[$1] = cases[$1] ? cases[$1] ", " $2 : $2; count[$1]++ }
  END { for (t in cases) print "- " t " (" count[t] "): " cases[t] }
' | sort
echo

if tr -d '\r' < "$3" | grep -qx 'Changes generated code\.'; then
  echo "The description says the change changes generated code."
else
  echo "The description does not say so. Add the line \`Changes generated code.\` to it if the change is meant, or fix the change if it is not."
  exit 1
fi
