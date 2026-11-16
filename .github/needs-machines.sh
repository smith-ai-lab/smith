#!/bin/sh
# Prints the machines whose job must run, one a line, in the order of
# `machines`, which sits beside this script.
# Usage: needs-machines.sh <base> <head>
# Each argument is a file of what `cargo test -- --fingerprint` prints for a
# compiler, one line for each target: the target and a hash of every
# executable the tests that depend on the machine would run for it. <base> is
# the compiler of the commit the change is based on, where those tests passed,
# and <head> is the change's.
# A machine is named when its target's line differs between the two, or is
# missing from either. A file with no line in it, as when the compiler of that
# commit could not make one, names every machine, since nothing is known.
set -eu

if [ "$#" -ne 2 ]; then
  echo "usage: needs-machines.sh <base> <head>" >&2
  exit 2
fi

machines=$(dirname "$0")/machines

awk -v base="$1" -v head="$2" '
  function load(file, into,    line, parts, n) {
    n = 0
    while ((getline line < file) > 0) {
      if (split(line, parts, " ") == 2) { into[parts[1]] = parts[2]; n++ }
    }
    close(file)
    return n
  }
  BEGIN {
    unknown = (load(base, was) == 0) || (load(head, now) == 0)
  }
  /^#/ || NF != 2 { next }
  { if (unknown || !($1 in was) || !($1 in now) || was[$1] != now[$1]) print $2 }
' "$machines"
