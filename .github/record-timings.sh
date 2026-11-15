#!/bin/sh
# Prints one history line for the commit checked out: the commit, the date,
# how the compiler built and tested, and how every case in
# bootstrap/tests/cases that builds did on every target this machine can
# build for. Programs are run for the target of this machine only.
# Usage: record-timings.sh [--header]
# Environment: JOB_START is when the job began, in milliseconds since the epoch.
set -eu

header='commit,date,compiler_build_ms,compiler_bytes,test_ms,job_ms,programs'
if [ "${1:-}" = "--header" ]; then
  echo "$header"
  exit 0
fi

root=$(cd "$(dirname "$0")/.." && pwd)
job_start=${JOB_START:-$(($(date +%s%N) / 1000000))}

now() { echo $(($(date +%s%N) / 1000000)); }

host=
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) host=linux_x64 ;;
  Darwin-arm64) host=macos_arm64 ;;
esac

cd "$root/bootstrap"

begin=$(now)
cargo build --release --locked --quiet
compiler_build=$(($(now) - begin))
smith="$root/bootstrap/target/release/smith"
compiler_bytes=$(wc -c <"$smith" | tr -d ' ')

begin=$(now)
cargo test --locked --quiet >/dev/null
test_ms=$(($(now) - begin))

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
programs=
for out in "$root"/bootstrap/tests/cases/*.out; do
  case_name=$(basename "$out" .out)
  source="$root/bootstrap/tests/cases/$case_name.smith"
  for target in linux_x64 macos_arm64; do
    dir="$work/$case_name.$target"
    mkdir "$dir"
    # A target the compiler cannot build for yet fails here and is skipped.
    if ! (cd "$dir" && "$smith" build --timings --target "$target" "$source" 2>"$dir/stages"); then
      continue
    fi
    while read -r stage time unit; do
      programs="$programs $case_name/$target/${stage}_ms=${time}"
    done <"$dir/stages"
    programs="$programs $case_name/$target/bytes=$(wc -c <"$dir/$case_name" | tr -d ' ')"
    if [ "$target" = "$host" ]; then
      begin=$(now)
      "$dir/$case_name" >/dev/null 2>&1 || true
      programs="$programs $case_name/$target/run_ms=$(($(now) - begin))"
    fi
  done
done

commit=$(git rev-parse HEAD)
date=$(date -u +%Y-%m-%dT%H:%M:%SZ)
job_ms=$(($(now) - job_start))
echo "$commit,$date,$compiler_build,$compiler_bytes,$test_ms,$job_ms,${programs# }"
