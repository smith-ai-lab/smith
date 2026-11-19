#!/bin/sh
# Checks that no file of the repository is an executable or a module a
# compiler built: one that begins as an ELF, Mach-O, or PE file or as a
# WebAssembly module does. `smith build` and `smith run` write such a file
# beside the source, and one left there is easily committed.
# Usage: check-no-executables.sh
set -eu

found=$(git ls-files | while IFS= read -r file; do
  [ -f "$file" ] || continue
  magic=$(head -c 4 "$file" | od -An -tx1 | tr -d ' \n')
  case "$magic" in
    7f454c46 | cffaedfe | cefaedfe | feedfacf | feedface | 4d5a* | 0061736d)
      echo "not ok: $file is a built executable or module; take it out of the commit"
      ;;
  esac
done)
if [ -n "$found" ]; then
  printf '%s\n' "$found"
  exit 1
fi
