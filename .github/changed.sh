#!/bin/sh
# Prints true when a change touches a file the compiler or its tests are built
# from, or what decides the run, and false when it does not. That is every file
# under bootstrap/, compiler/, which the tests of bootstrap/ hold to the Rust
# compiler, and .github/, install.sh, which the tests run, and .gitattributes,
# which sets the bytes of the test cases on checkout. A change to knowledge/ or
# .agents/ alone gives false.
# Usage: changed.sh <base> <head>
set -eu

if git diff --name-only "$1" "$2" | grep -Eq '^(bootstrap/|compiler/|\.github/|\.gitattributes$|install\.sh$)'; then
  echo true
else
  echo false
fi
