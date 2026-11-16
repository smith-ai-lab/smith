#!/bin/sh
# Decides whether continuous integration passed.
# Usage: ci-result.sh <job>=<result>...
# A result is what a job gives: success, failure, cancelled, or skipped.
# Passes when every job succeeded or was skipped on purpose.
# Fails when any job failed or was cancelled, and when given no jobs.
set -eu

if [ "$#" -eq 0 ]; then
  echo "no jobs to decide on"
  exit 1
fi

status=0
for entry in "$@"; do
  job=${entry%%=*}
  result=${entry#*=}
  case "$result" in
    success | skipped) echo "ok: $job $result" ;;
    *)
      echo "not ok: $job $result"
      status=1
      ;;
  esac
done
exit "$status"
