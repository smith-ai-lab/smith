#!/bin/sh
# The files whose result changed between two count tables (#2879): count_diff.sh <main results.tsv> <this results.tsv> <main sha7>
# A table has one or more rows per file: path, stage (`pass` or the stage that failed), ... A file's stage is its first row's;
# a file one table lacks counts as failing there. Prints one line per changed file, a total, or the no-change line.
# Never fails: the comparison is output only.
awk -F'\t' -v base="$3" '
  FNR == 1 { f++ }
  !((f, $1) in st) { st[f, $1] = ($2 == "pass") ? "pass" : "fail"; all[$1] = 1 }
  END {
    for (p in all) {
      a = ((1, p) in st) ? st[1, p] : "fail"; b = ((2, p) in st) ? st[2, p] : "fail"
      if (a == b) continue
      if (a == "pass") { out[++n] = "count: pass->fail " p; down++ } else { out[++n] = "count: fail->pass " p; up++ }
    }
    if (n == 0) { print "count: no file changed against main " base; exit }
    for (i = 1; i <= n; i++) print out[i]
    printf "count: %d files changed (%d pass->fail, %d fail->pass)\n", n, down + 0, up + 0
  }' "$1" "$2" | LC_ALL=C sort -t' ' -k2,2 -s | awk '/^count: [0-9]|^count: no file/ {tail = $0; next} {print} END {if (tail != "") print tail}'
exit 0
