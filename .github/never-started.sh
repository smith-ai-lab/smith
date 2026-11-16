#!/bin/sh
# Decides what to do about jobs that ended without ever starting.
# Usage: never-started.sh retry < states
#        never-started.sh result <machine>... < states
# Each line of the input is one job, oldest first, as three fields separated by
# a tab: the kind of machine it asked for, what it ended as, and how many steps
# it ran. A job that ended as `cancelled` with no steps never started: no
# machine was found for it, as opposed to a job that was stopped while running.
#   retry   prints the machines whose newest job never started and which have
#           not yet been tried three times, one a line, in order of first
#           appearance. Those jobs are started again, so each is started at
#           most twice more.
#   result  takes the machines that must be seen as arguments, and prints
#           `success` only when the newest job of each of them succeeded.
#           Otherwise it prints what the first that did not ended as, or
#           `missing` for a machine with no job in the list, or `unlabelled`
#           when any job that was not skipped names no machine. A skipped job
#           that names none is the place of a job that never ran, and counts
#           for nothing. Nothing but a success passes, so a
#           list that could not be read, or is empty, never does. It exits
#           with 2 when given no machine.
set -eu

mode=${1:-}
case "$mode" in
  retry) ;;
  result)
    if [ "$#" -lt 2 ]; then
      echo "result needs the machines that must be seen" >&2
      exit 2
    fi
    ;;
  *)
    echo "usage: never-started.sh retry|result <machine>... < states" >&2
    exit 2
    ;;
esac
shift

awk -F '\t' -v mode="$mode" -v wanted="$*" '
  {
    machine = $1
    if (machine == "" && $2 != "skipped") unlabelled = 1
    if (!(machine in seen)) {
      seen[machine] = 1
      order[++count] = machine
    }
    lost = ($2 == "cancelled" && $3 + 0 == 0)
    if (lost) tries[machine]++
    last[machine] = $2
    lastlost[machine] = lost
  }
  END {
    if (mode == "retry") {
      for (i = 1; i <= count; i++) {
        m = order[i]
        if (m != "" && lastlost[m] && tries[m] <= 2) print m
      }
      exit
    }
    if (unlabelled) { print "unlabelled"; exit }
    n = split(wanted, want, " ")
    for (i = 1; i <= n; i++) {
      m = want[i]
      if (!(m in seen)) { print "missing"; exit }
      if (last[m] != "success") { print (last[m] == "" ? "unfinished" : last[m]); exit }
    }
    print "success"
  }
'
