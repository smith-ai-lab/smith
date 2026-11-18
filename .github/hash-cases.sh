#!/bin/sh
# Prints the hash of the executable of every case for every target, one line
# each, as `<target> <case file> <sha256>`, in order. A case that does not
# build for a target has no line for it.
# Usage: hash-cases.sh <smith> <cases folder>
# <smith> is a built compiler, so that any commit's can be asked, and its list
# of targets is read from what it says when it is given one it does not know.
set -eu

if [ "$#" -ne 2 ]; then
  echo "usage: hash-cases.sh <smith> <cases folder>" >&2
  exit 2
fi

smith=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
cases=$(cd "$2" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

targets=$("$smith" build --target none "$cases/hello.smith" 2>&1 |
  sed -n 's/.*the targets are //p' | tr -d ',')
if [ -z "$targets" ]; then
  echo "hash-cases.sh: $smith names no targets" >&2
  exit 1
fi

hash() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"; else shasum -a 256 "$1"; fi |
    cut -d ' ' -f 1
}

for target in $targets; do
  for source in "$cases"/*.smith; do
    file=$(basename "$source")
    rm -rf "$work/case"
    mkdir "$work/case"
    cp "$source" "$work/case/$file"
    if (cd "$work/case" && "$smith" build --target "$target" "$file") >/dev/null 2>&1; then
      for built in "$work/case"/*; do
        [ "$(basename "$built")" = "$file" ] && continue
        echo "$target $file $(hash "$built")"
      done
    fi
  done
done
