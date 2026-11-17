#!/bin/sh
# The sweep: small changes to the compiler, each run against every test, to
# find which break each test alone catches.
#
#   sh sweep/sweep.sh list              print the changes, one per line
#   sh sweep/sweep.sh run [workers]     run them, writing sweep/results
#   sh sweep/sweep.sh report            print, for each test, the changes it
#                                       alone catches, from sweep/results
#
# Run in `bootstrap/`. A change is `<file>:<line>:<operator>`: the first
# place on that line where the operator's left side stands is replaced by
# its right side. The changes are chosen by a hash of the file, the line, and
# the operator, so the list is the same on every run, and it grows when
# `SWEEP_ONE_IN` is made smaller. The tests run with the seed in
# `SMITH_SEED`, which is 0 unless it is set, so that a change is caught or
# not caught the same way each time.
#
# A change that does not build is not counted. A line of `results` is
# `<change> <tests that failed, separated by spaces>`, or `<change> -` when
# every test passed, or `<change> crashed` when the run ended with no summary, or
# `<change> hang` when the tests ran for more than
# `SWEEP_TIMEOUT` seconds.
set -eu

ONE_IN=${SWEEP_ONE_IN:-5}
TIMEOUT=${SWEEP_TIMEOUT:-300}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)

# The operators, `<left>@<right>`, where no side has an `@`. A space is
# written `~` in the name of a change, so that a change is one word.
operators() {
  cat <<'OPS'
 + @ - 
 - @ + 
 * @ + 
 < @ <= 
 <= @ < 
 > @ >= 
 >= @ > 
 == @ != 
 != @ == 
 && @ || 
 || @ && 
true@false
false@true
+ 1@+ 0
- 1@- 0
+ 1@+ 2
.min(@.max(
.max(@.min(
.len() > 0@.len() > 1
is_empty()@!is_empty()
.rev()@
.is_some()@.is_none()
.is_none()@.is_some()
<<@>>
>>@<<
OPS
}

list() {
  cd "$root"
  find src -name '*.rs' | sort | while read -r file; do
    operators | while IFS='@' read -r left right rest; do
      [ -n "$left" ] || continue
      # `&` is the operator that drops a borrow; `|=` the other that is `|`.
      awk -v file="$file" -v left="$left" -v right="$right" -v one_in="$ONE_IN" '
        BEGIN { for (i = 1; i < 256; i++) ord[sprintf("%c", i)] = i }
        function hash(s,   i, h) {
          h = 7
          for (i = 1; i <= length(s); i++) h = (h * 31 + ord[substr(s, i, 1)]) % 1000003
          return h
        }
        /^[ \t]*\/\// { next }
        {
          line = $0
          sub(/\/\/.*/, "", line)
          if (index(line, "\"") > 0) next
          if (index(line, left) == 0) next
          id = file ":" NR ":" left "@" right
          gsub(/ /, "~", id)
          if (hash(id) % one_in == 0) print id
        }' "$file"
    done
  done
}

change_file() { echo "$1" | cut -d: -f1; }
change_line() { echo "$1" | cut -d: -f2; }
change_op() { echo "$1" | cut -d: -f3-; }

mutate() {
  file=$(change_file "$1")
  line=$(change_line "$1")
  op=$(change_op "$1")
  op=$(echo "$op" | tr '~' ' ')
  left=${op%%@*}
  right=${op#*@}
  awk -v line="$line" -v left="$left" -v right="$right" '
    NR == line {
      at = index($0, left)
      if (at > 0) $0 = substr($0, 1, at - 1) right substr($0, at + length(left))
    }
    { print }' "$file" >"$file.new"
  mv "$file.new" "$file"
}

# Runs the tests on the copy of the compiler in the directory, with one
# change applied, and prints the line of `results`.
try() {
  copy=$1
  change=$2
  (
    cd "$copy"
    rm -rf src
    cp -r src.orig src
    mutate "$change"
    if ! cargo test --locked --no-run --message-format=json >build.json 2>build.log; then
      echo "$change build-failed"
      exit 0
    fi
    exe=$(grep -o '"executable":"[^"]*/deps/tests-[^"]*"' build.json | tail -1 | cut -d'"' -f4)
    # A change that makes a test ask for all the memory ends the run, so the
    # run is given a limit and a run with no summary counts as `crashed`.
    ulimit -v 10000000
    out=$(SMITH_SEED=${SMITH_SEED:-0} timeout "$TIMEOUT" "$exe" 2>&1) && status=0 || status=$?
    if [ "$status" = 124 ]; then
      echo "$change hang"
      exit 0
    fi
    if ! echo "$out" | grep -q ' passed, '; then
      echo "$change crashed"
      exit 0
    fi
    failed=$(echo "$out" | sed -n 's/^failed: //p' | tr '\n' ' ')
    echo "$change ${failed:--}"
  )
}

run() {
  workers=${1:-2}
  cd "$root"
  list >"$here/changes"
  : >"$here/results.part"
  rm -f "$here"/results.part.*
  i=0
  while [ "$i" -lt "$workers" ]; do
    (
      # The tests read files beside `bootstrap/`, so the copy is of the
      # whole repository.
      whole=$(mktemp -d)
      cp -r "$root/.." "$whole/"
      copy=$whole/bootstrap
      cp -r "$copy/src" "$copy/src.orig"
      n=0
      while read -r change; do
        if [ $((n % workers)) -eq "$i" ]; then
          try "$copy" "$change" >>"$here/results.part.$i"
        fi
        n=$((n + 1))
      done <"$here/changes"
      rm -rf "$whole"
    ) &
    i=$((i + 1))
  done
  wait
  cat "$here"/results.part.* | sort >"$here/results"
  rm -f "$here"/results.part.* "$here/results.part"
}

# For each test, the changes that only it catches.
report() {
  awk '
    $2 == "build-failed" || $2 == "hang" || $2 == "crashed" { next }
    {
      caught[$1] = NF - 1
      if ($2 == "-") { caught[$1] = 0; next }
      for (i = 2; i <= NF; i++) { tests[$i] = 1; by[$i] = by[$i] " " $1 }
      if (NF == 2) { alone[$2] = alone[$2] "\n    " $1; count[$2]++ }
      total++
      if (NF > 1 && $2 != "-") caughtN++
    }
    END {
      print caughtN " of " total " changes that build are caught by some test"
      for (t in tests) {
        printf "%s: %d alone\n", t, count[t] + 0
        if (alone[t] != "") print alone[t]
      }
    }' "$here/results" | sort
}

case "${1:-}" in
  list) list ;;
  run) shift; run "$@" ;;
  report) report ;;
  *) echo "usage: sweep.sh list | run [workers] | report" >&2; exit 2 ;;
esac
