#!/bin/sh
# Builds Ninja from its pinned source, for the builds that `smith build llvm` runs (LLVM's CMake, Z3, Alive2 and Bitwuzla's
# Meson all want one), so that nothing comes from Homebrew. Usage: ninja.sh <ninja source> <python> <output file>. The
# output is $CACHE/smith/llvm/<name>/tools/ninja, made once. Ninja builds itself with Python and the system's C++
# compiler, in a copy of the source (a package is never written to).
set -eu
src=$1 python=$2 out=$3
[ -x "$out" ] && exit 0
work=$(mktemp -d "${TMPDIR:-/tmp}/ninja.XXXXXX")
trap 'rm -rf "$work"' EXIT INT TERM
cp -R "$src/." "$work/"
chmod -R u+w "$work"
mkdir -p "$(dirname "$out")"
(cd "$work" && CXX=/usr/bin/clang++ "$python/bin/python3" -I configure.py --bootstrap >/dev/null)
cp "$work/ninja" "$out.part"
mv "$out.part" "$out"
