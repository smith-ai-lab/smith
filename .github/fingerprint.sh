#!/bin/sh
# Prints the fingerprint of the compiler in a checkout, for needs-machines.sh.
# Usage: fingerprint.sh <checkout>
# It prints nothing, and still succeeds, when that commit cannot make one, as
# when it is older than the fingerprint, so that nothing is known of it.
set -u

cd "$1/bootstrap" || exit 0
cargo test --locked -- --fingerprint 2>/dev/null | grep -E '^[a-z0-9_]+ [0-9a-f]{64}$' || true
