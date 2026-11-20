#!/bin/sh
# Makes what `decide` of `ci` needs of the commit a change is based on: the
# fingerprint of its compiler, as `fingerprint.sh` prints it, and the compiler.
# Usage: base-results.sh <checkout> <folder>
# The folder gets `fingerprint` and `smith`.
set -eu

if [ "$#" -ne 2 ]; then
  echo "usage: base-results.sh <checkout> <folder>" >&2
  exit 2
fi

mkdir -p "$2"
sh "$(dirname "$0")/fingerprint.sh" "$1" > "$2/fingerprint"
(cd "$1/bootstrap" && cargo build --locked)
cp "$1/bootstrap/target/debug/smith" "$2/smith"
