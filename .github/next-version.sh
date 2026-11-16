#!/bin/sh
# Works out the version of the next release from the subjects of the commits
# since the last one, and from nothing else.
# Usage: next-version.sh <last version, or "none"> < subjects, one on each line
# Prints the next version, or nothing when the commits do not change what the
# compiler does. While the first number is 0, a breaking commit (`!`) or a
# `feat` raises the second number and sets the third to 0, and otherwise a
# `fix` or a `perf` raises the third. A release is made only when some
# commit is a `feat`, a `fix`, or a `perf`.
set -eu

last=${1:-none}
if [ "$last" = "none" ]; then
  major=0
  minor=0
  patch=0
  first=1
else
  case "$last" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *)
      echo "not a version: $last" >&2
      exit 2
      ;;
  esac
  major=${last%%.*}
  rest=${last#*.}
  minor=${rest%%.*}
  patch=${rest#*.}
  first=0
fi
if [ "$major" != "0" ]; then
  echo "versions from 1.0.0 on are not defined yet: $last" >&2
  exit 2
fi

releases=0
raises_minor=0
raises_patch=0
while IFS= read -r subject || [ -n "$subject" ]; do
  type=$(printf '%s\n' "$subject" | sed -n -E 's/^([a-z]+)(\([a-z0-9._/-]+\))?(!?): .*/\1/p')
  bang=$(printf '%s\n' "$subject" | sed -n -E 's/^([a-z]+)(\([a-z0-9._/-]+\))?(!?): .*/\3/p')
  case "$type" in
    feat)
      releases=1
      raises_minor=1
      ;;
    fix | perf)
      releases=1
      raises_patch=1
      ;;
  esac
  if [ -n "$bang" ]; then
    raises_minor=1
  fi
done

if [ "$releases" = 0 ]; then
  exit 0
fi
if [ "$first" = 1 ]; then
  echo "0.1.0"
elif [ "$raises_minor" = 1 ]; then
  echo "0.$((minor + 1)).0"
else
  echo "0.$minor.$((patch + 1))"
fi
