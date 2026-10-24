#!/bin/sh
# Soundness tests of `smith prove` (issue #64). Usage: sh tests/prove_sound.sh path/to/smith [full]
# 1. Run what was proved: tests/prove/sound/<case>.c is a harness that includes a case (or a library of smith) and calls
#    each function smith prove proves on random inputs that satisfy its smith_requires, under ASan and UBSan; a failed
#    ensures, invariant, assert or sanitizer report is a wrong "proved" (tests/prove/sound/sound.h). The script first
#    asks smith prove for `ok` on each function the harness lists on its `// proves:` line, so the harness tests what
#    was proved and not what merely compiles.
# 2. False claims: tests/prove/unsound/<case>.c holds functions whose contract is false, each with the input that
#    refutes it (`// unsound: <function> <text the refuting run prints>`). smith prove must fail the function and the
#    refuting run must die with the text.
# `full` runs each harness on 8 seeds of 50000 inputs (about a minute), the default on one seed of 2000 (a few seconds).
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
mode=${2:-}
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
export XDG_CACHE_HOME=$work/xdg-cache # the module and package caches of the tests, never the real ones
proof_cache=${SMITH_TEST_CACHE:-$HOME/.cache/smith-tests}/proofs # the proofs are kept between runs, as in tests/prove.sh
mkdir -p "$proof_cache" "$XDG_CACHE_HOME/smith" && ln -s "$proof_cache" "$XDG_CACHE_HOME/smith/proofs"
failures=0
total=0

check() { # name, status (0 is ok), details
    total=$((total + 1))
    if [ "$2" -eq 0 ]; then
        echo "ok    $1"
    else
        failures=$((failures + 1))
        echo "FAIL  $1"
        printf '%s\n' "$3" | sed 's/^/  /'
    fi
}

"$smith" prove "$here/tests/prove/sound/clamp.c" -- -I "$here/include" 2>&1 | grep -q "has no prover" && {
    echo "skipped: this smith has no prover"
    exit 0
}
# Two harnesses reach the modules' static functions by including their raw lowering (`smith cc --smith-lower`).
mkdir "$work/lowered" || exit 1
for module in arena random; do
    "$smith" cc --smith-lower "$here/lib/smith/$module.smith" >"$work/lowered/${module}_lowered.c" || exit 1
done
flags="-std=gnu23 -g -O0 -fsanitize=address,undefined -fno-sanitize-recover=all -ftrivial-auto-var-init=pattern -I $here/include -I $work/lowered -w"
common="$here/lib/trace.c $here/lib/smith/contract.smith"
cd "$here" || exit 1

# 1. The harnesses: one job each (prove, build, run), side by side.
if [ "$mode" = full ]; then seeds="1 2 3 4 5 6 7 8"; count=50000; else seeds=1; count=2000; fi
harness() { # source file of the harness
    name=$(basename "$1" .c)
    log=$work/$name.log
    proves=$(sed -n 's,^// proves: *,,p' "$1")
    files=$(sed -n 's,^// prove-files: *,,p' "$1")
    link=$(sed -n 's,^// link: *,,p' "$1")
    [ -n "$files" ] || files=$1
    out=$("$smith" prove -v $files -- -I "$here/include" -I "$work/lowered" 2>&1)
    for fn in $proves; do
        case $out in
        *"ok    $fn  ("*) ;;
        *)
            echo "the prover does not prove $fn any more (a harness tests what was proved):" >"$log"
            printf '%s\n' "$out" | grep -E "FAIL|smith:" | head -3 >>"$log"
            return 1
            ;;
        esac
    done
    mkdir -p "$work/$name"
    sources=
    for source in $link; do
        case $source in
        *.smith) # a module is copied, so the header smith writes beside it is not written in the repository
            mkdir -p "$work/$name/$(dirname "${source#tests/prove/}")"
            cp "$source" "$work/$name/${source#tests/prove/}"
            sources="$sources $work/$name/${source#tests/prove/}"
            ;;
        *) sources="$sources $source" ;;
        esac
    done
    "$smith" cc $flags "$1" $sources $common -o "$work/$name/run" >>"$log" 2>&1 || return 1
    for seed in $seeds; do
        "$work/$name/run" "$seed" "$count" >>"$log" 2>&1 || {
            echo "seed $seed:" >>"$log"
            return 1
        }
    done
}
for source in tests/prove/sound/*.c; do
    (harness "$source" || exit 1) &
    echo "$! $source" >>"$work/jobs"
done
while read -r pid source; do
    name=$(basename "$source" .c)
    wait "$pid"
    status=$?
    count_ok=$(grep -c '^ok ' "$work/$name.log" 2>/dev/null)
    check "run what was proved: $name ($count_ok runs)" "$status" "$(tail -8 "$work/$name.log")"
done <"$work/jobs"

# 2. The false claims.
corpus() { # source file of the corpus
    name=$(basename "$1" .c)
    log=$work/unsound-$name.log
    out=$("$smith" prove -v "$1" -- -I "$here/include" 2>&1)
    "$smith" cc $flags "$1" $common -o "$work/unsound-$name" >"$log" 2>&1 || return 1
    sed -n 's,^// \(unsound[-a-z0-9]*\): ,\1 ,p' "$1" | while read -r kind function text; do
        case $kind in
        unsound)
            case $out in
            *"FAIL  $function  ("*) ;;
            *) echo "$function: smith prove should fail it, and says: $(printf '%s\n' "$out" | tail -1)" >>"$log" ;;
            esac
            ;;
        esac
        run=$("$work/unsound-$name" "$function" 2>&1)
        status=$?
        if [ "$status" -eq 0 ]; then
            echo "$function: the run that should refute it exits 0" >>"$log"
        else
            case $run in
            *"$text"*) ;;
            *) echo "$function: the refuting run does not print '$text': $(printf '%s\n' "$run" | head -2)" >>"$log" ;;
            esac
        fi
    done
    [ ! -s "$log" ]
}
: >"$work/jobs"
for source in tests/prove/unsound/*.c; do
    (corpus "$source" || exit 1) &
    echo "$! $source" >>"$work/jobs"
done
while read -r pid source; do
    name=$(basename "$source" .c)
    wait "$pid"
    status=$?
    claims=$(grep -c '^// unsound' "$source")
    check "false claims: $name ($claims refuted)" "$status" "$(head -8 "$work/unsound-$name.log")"
done <"$work/jobs"

echo "$((total - failures)) of $total prove soundness checks passed"
[ "$failures" -eq 0 ]
