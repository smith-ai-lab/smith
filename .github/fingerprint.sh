#!/bin/sh
# Prints the fingerprint of the compiler in a checkout, for needs-machines.sh.
# Usage: fingerprint.sh <checkout>
# It fails, and says why, when the fingerprint cannot be made: when the build
# or the run fails, and then it prints what they said, or when the run gives no
# line of one. It never prints nothing and succeeds, which would name every
# machine without a word.
set -u

if [ "$#" -ne 1 ]; then
  echo "usage: fingerprint.sh <checkout>" >&2
  exit 2
fi

cd "$1/bootstrap" || exit 1
out=$(mktemp)
err=$(mktemp)
trap 'rm -f "$out" "$err"' EXIT

if ! cargo test --locked -- --fingerprint > "$out" 2> "$err"; then
  echo "fingerprint.sh: the fingerprint of the compiler in $1 cannot be made:" >&2
  cat "$err" >&2
  exit 1
fi
if ! grep -E '^[a-z0-9_]+ [0-9a-f]{64}$' "$out"; then
  echo "fingerprint.sh: the run of the compiler in $1 gave no line of a fingerprint:" >&2
  cat "$out" "$err" >&2
  exit 1
fi
