#!/bin/sh
# Says which release files GitLab lacks. Reads what GitHub holds and what
# GitLab holds, each a file of lines `<version> <file> <sha256>`, and prints
# the lines GitHub has and GitLab lacks. A file both hold with different
# hashes is named on stderr and the exit status is 1, and nothing is printed.
# Usage: plan-uploads.sh <github lines> <gitlab lines>
set -eu

awk '
  NF == 0 { next }
  NR == FNR { held[$1 " " $2] = $3; next }
  {
    key = $1 " " $2
    if (!(key in held)) { missing[++n] = $0 }
    else if (held[key] != $3) { print "the hash of " $2 " in " $1 " differs on GitLab" > "/dev/stderr"; bad = 1 }
  }
  END {
    if (bad) exit 1
    for (i = 1; i <= n; i++) print missing[i]
  }
' "$2" "$1"
