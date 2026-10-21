#!/bin/sh
# Tests of `smith prove`, with the prover linked in (every full smith has it; the stage0 says it has none and the tests are skipped). Usage:
# sh tests/prove.sh path/to/smith
# The programs are tests/prove/*.c: the issue's clamp, a caller that breaks it, undefined behaviour, a column one past a row,
# and one program for each construct that is an error, never an assumption.
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
export XDG_CACHE_HOME=$work/xdg-cache # the module and package caches of the tests, never the real ones
failures=0
total=0
out=
rc=0

capture() {
    out=$("$@" 2>&1)
    rc=$?
}

expect() { # name, expected, actual
    total=$((total + 1))
    if [ "$2" = "$3" ]; then
        echo "ok    $1"
    else
        failures=$((failures + 1))
        echo "FAIL  $1"
        printf '  expected: %s\n' "$2"
        printf '  actual:   %s\n' "$3"
    fi
}

expect_contains() { # name, text, needle
    total=$((total + 1))
    case $2 in
    *"$3"*) echo "ok    $1" ;;
    *)
        failures=$((failures + 1))
        echo "FAIL  $1"
        printf '  expected to contain: %s\n' "$3"
        printf '  actual:              %s\n' "$2"
        ;;
    esac
}

prove() { # arguments of smith prove, then the compile flags
    capture "$smith" prove "$@" -- -I "$here/include"
}

capture "$smith" prove "$here/tests/prove/clamp.c" -- -I "$here/include"
case $out in
*"has no prover"*)
    echo "skipped: this smith has no prover"
    exit 0
    ;;
esac
cases=$here/tests/prove

# 1. The issue's clamp, and a function that uses it through its contract alone.
prove -v "$cases/clamp.c"
expect "clamp: proved" 0 "$rc"
expect_contains "clamp: ... both functions" "$out" "2 proved, 0 failed"

# 2. A caller that breaks the contract fails, with the line and the values.
prove "$cases/clamp.c" "$cases/bad_caller.c"
expect "caller: fails" 1 "$rc"
expect_contains "caller: ... at the call, with the contract it breaks" "$out" "bad_caller.c:7: the call of clamp may break smith_requires(low <= high)"
expect_contains "caller: ... and the values of the call" "$out" "clamp.low = 10 clamp.high = 5"

# 3. Undefined behaviour is a proof obligation, and a counterexample prints its values.
prove --function=add_unchecked "$cases/overflow.c"
expect "overflow: fails" 1 "$rc"
expect_contains "overflow: ... names the undefined behaviour" "$out" "overflow.c:8: undefined behaviour: signed integer addition overflow"
expect_contains "overflow: ... with values that overflow" "$out" "counterexample: a = "
prove --function=add --function=add_wrong "$cases/overflow.c"
expect_contains "ensures: a true one is proved" "$out" "1 proved, 1 failed"
expect_contains "ensures: a false one fails with its values" "$out" "smith_ensures(sum < a + b) may be false"
prove --function=add_wrong "$cases/overflow.c"
total=$((total + 1))
sum=$(printf '%s\n' "$out" | sed -n 's/.*a = \([0-9]*\) b = \([0-9]*\) sum = \([0-9]*\)$/\1 \2 \3/p')
set -- $sum
if [ $# -eq 3 ] && [ $(($1 + $2)) -eq "$3" ]; then
    echo "ok    ensures: the counterexample's values are consistent (a + b = sum)"
else
    failures=$((failures + 1))
    echo "FAIL  ensures: the counterexample's values are consistent: $sum"
fi

# 4. The column one past a row of 8 (checkers' get_user_move) is caught, per dimension.
prove --function=cell "$cases/board.c"
expect "board: row < 8 && col < 8 is proved" 0 "$rc"
prove --function=cell_wrong "$cases/board.c"
expect "board: col <= 8 fails" 1 "$rc"
expect_contains "board: ... as an array index out of bounds" "$out" "undefined behaviour: Array index out of bounds"
expect_contains "board: ... with column 8" "$out" "col = 8"

# 5. A function is proved through a callee's contract alone; the callee's body is assembly, which is an error of its own.
prove --function=scaled "$cases/assembly.c"
expect "assembly: scaled is proved without reading the body of sensor_percent" 0 "$rc"
prove --function=scaled_too_far "$cases/assembly.c"
expect "assembly: what the contract does not give fails" 1 "$rc"
expect_contains "assembly: ... with the value" "$out" "percent = "
prove --function=sensor_percent "$cases/assembly.c"
expect_contains "assembly: the callee itself cannot be proved" "$out" "inline assembly cannot be proved"

# 6. Each construct that cannot be proved is an error that says where and what to do.
prove "$cases/recursion.c"
expect_contains "recursion: is an error" "$out" "recursion.c:9: cannot be proved: recursion: countdown calls itself"
expect_contains "recursion: ... that says what to do" "$out" "write it as a loop with a constant bound"
prove "$cases/pointer.c"
expect_contains "function pointer: is an error" "$out" "pointer.c:10: cannot be proved: a call through a function pointer"
prove "$cases/nobody.c"
expect_contains "no body: is an error" "$out" "nobody.c:10: cannot be proved: a call of lookup, which has neither a body nor a contract"
prove "$cases/unbounded.c"
expect_contains "loop: with no bound is an error" "$out" "unbounded.c:8: cannot be proved: a loop with no constant bound"
prove "$cases/message.m"
expect_contains "Objective-C: a message send is an error" "$out" "message.m:10: cannot be proved: an Objective-C message send"

# 7. A proof that runs out of time fails, like a test that times out.
prove --budget=2 "$cases/division.c"
expect "budget: fails" 1 "$rc"
expect_contains "budget: ... as a timeout" "$out" "timed out after"

# 7b. A proof's process ends with its parent: kill the parent (a timeout around the command, a closed terminal) while a
# proof runs and the process of the proof must be gone a moment later, not run on for ever with the parent's pid 1.
"$smith" prove --budget=30 "$cases/division.c" -- -I "$here/include" >/dev/null 2>&1 &
parent=$!
sleep 3
children=$(pgrep -P "$parent")
kill -9 "$parent"
wait "$parent" 2>/dev/null
sleep 1
alive=
for child in $children; do
    kill -0 "$child" 2>/dev/null && alive="$alive $child"
done
[ -n "$children" ] && expect "orphan: a proof was running when its parent was killed" yes yes || expect "orphan: a proof was running" yes "no child"
expect "orphan: the proof's process ends with its parent" "" "$alive"
for child in $alive; do kill -9 "$child" 2>/dev/null; done

# 7c. A caller assumes the callee's smith_ensures about the memory the callee wrote (issue #41), and nothing else.
for function in five set_five three_then_bump bump two push two_through_a_pointer uses_bump_twice_read bump_twice_read; do
    prove --function=$function "$cases/memory.c"
    expect "memory: $function is proved" 0 "$rc"
done
prove --function=too_much "$cases/memory.c"
expect "memory: a caller that wants more than the callee promises fails" 1 "$rc"
expect_contains "memory: ... at its own smith_ensures" "$out" "memory.c:47: smith_ensures(value == 4) may be false"
expect_contains "memory: ... with a value the callee allows" "$out" "counterexample: value = "
for function in relies_on_the_body clobbered_local clobbered_parameter; do
    prove --function=$function "$cases/memory.c"
    expect "memory: $function relies on what the callee does not promise and fails" 1 "$rc"
    expect_contains "memory: ... with a counterexample, not poison and not undefined behaviour" "$out" "may be false"
    expect_contains "memory: ... that is a value" "$out" "first = "
    case $out in *poison* | *"undefined behaviour"*) expect "memory: $function names no poison" "none" "$out" ;; esac
done

# 7d. A struct passed by value holds values (issue #41); its bytes are free, never poison.
for function in apply_move count_of bump_count is_step; do
    prove --function=$function "$cases/by_value.c"
    expect "by value: $function is proved" 0 "$rc"
done
prove --function=same_row "$cases/by_value.c"
expect "by value: a wrong promise about the fields fails" 1 "$rc"
expect_contains "by value: ... as a promise that may be false" "$out" "by_value.c:30: smith_ensures(move.row == move.dest_row) may be false"
case $out in *poison* | *"undefined behaviour"*) expect "by value: the counterexample is not poison" "a value" "$out" ;; *) expect "by value: the counterexample is not poison" "a value" "a value" ;; esac

# 7e. A file that does not compile is an error with clang's message, never "nothing to prove".
prove "$cases/syntax_error.c"
expect "compile error: fails" 1 "$rc"
expect_contains "compile error: ... with clang's message" "$out" "syntax_error.c:2:26: error: expected expression"
case $out in *"nothing to prove"*) expect "compile error: is not nothing to prove" "an error" "$out" ;; *) expect "compile error: is not nothing to prove" "an error" "an error" ;; esac
prove "$cases/memory.c" "$cases/syntax_error.c"
expect "compile error: in the second file fails" 1 "$rc"
prove "$work/does-not-exist.c"
expect "missing file: fails" 1 "$rc"
expect_contains "missing file: ... and names it" "$out" "does-not-exist.c"

# 7f. A function with a variable argument list does not stop the others; calling it is an error that says so.
prove --function=twice "$cases/varargs.c"
expect "varargs: another function is proved" 0 "$rc"
prove --function=uses_sum "$cases/varargs.c"
expect "varargs: calling one is an error" 1 "$rc"
expect_contains "varargs: ... that names the call" "$out" "varargs.c:25: cannot be proved: a call of sum, which takes a variable argument list"

# 8. checkers' move generator, with the contracts of the issue: the fixed one is proved within the budget, and the one
# before the fixes (else-if, col > 1) fails with a counterexample.
prove --function=get_valid_diagonals "$cases/diagonals.c"
expect "checkers: the fixed get_valid_diagonals is proved in the budget" 0 "$rc"
prove --function=get_valid_diagonals "$cases/diagonals_prefix.c"
expect "checkers: the one before its fixes fails" 1 "$rc"
expect_contains "checkers: ... on the legal moves it misses" "$out" "added_every_legal_step"
expect_contains "checkers: ... with a counterexample" "$out" "counterexample: "

# 9. Nothing to prove, and the counts of a run.
printf 'int main(void) { return 0; }\n' >"$work/plain.c"
prove "$work/plain.c"
expect_contains "no contract: nothing is proved, and it says so" "$out" "nothing to prove: no function with smith_requires"
expect "no contract: and that is a success" 0 "$rc"
prove --function=nothing "$work/plain.c"
expect_contains "--function that matches nothing says so" "$out" "by that name"
expect "--function that matches nothing fails" 1 "$rc"

# 10. A --function that names no function the prover read is an error, never a skip.
prove --function=harness_kept --function=harness_dropped "$cases/static_harness.c"
expect "--function of an unemitted static: fails" 1 "$rc"
expect_contains "... and names it" "$out" "--function=harness_dropped: no function by that name"
expect_contains "... with the likely cause" "$out" "static function that nothing calls"
expect_contains "... and the fix" "$out" "[[gnu::used]]"
prove --function=harness_kept "$cases/static_harness.c"
expect "--function of a kept static: proved" 0 "$rc"
prove --function=no_contract "$cases/static_harness.c"
expect "--function of a function with no contract: fails" 1 "$rc"
expect_contains "... says there is nothing to prove" "$out" "so there is nothing to prove"

# 11. A module written in .smith is compiled as `smith cc` compiles it, and its functions are named as written
# (geometry::box::clamp, never geometry_box_clamp), in the report, in -v and in --function.
if "$smith" cc --smith-which -x c 2>/dev/null | grep -q "token-hook yes"; then
    box=$cases/geometry/box.smith
    prove -v --function=geometry::box::clamp "$box"
    expect "smith module: a contract in a .smith function is proved" 0 "$rc"
    expect_contains "smith module: ... named as written, with its line" "$out" "ok    geometry::box::clamp  ("
    expect_contains "smith module: ... with its line" "$out" "geometry/box.smith:5)"
    prove --function=geometry::box::clamp_wrong "$box"
    expect "smith module: a contract that is false fails" 1 "$rc"
    expect_contains "smith module: ... named as written" "$out" "FAIL  geometry::box::clamp_wrong"
    expect_contains "smith module: ... at the .smith line" "$out" "geometry/box.smith:15: smith_ensures(result < high) may be false"
    expect_contains "smith module: ... with the values" "$out" "counterexample: high = "
    prove --function=geometry::box::add_unchecked "$box"
    expect_contains "smith module: undefined behaviour is at the .smith line" "$out" "geometry/box.smith:21: undefined behaviour: signed integer addition overflow"
    prove --function=geometry::box::caller "$box"
    expect_contains "smith module: a caller that breaks a contract names the callee as written" "$out" "the call of geometry::box::clamp may break smith_requires(low <= high)"
    expect_contains "smith module: ... and the values by their written names" "$out" "geometry::box::clamp.low = 10 geometry::box::clamp.high = 5"
    prove "$box"
    expect_contains "smith module: all of them" "$out" "1 proved, 3 failed"
else
    echo "skipped: this smith's clang has no token hook, so no .smith proofs"
fi

echo
echo "$total prove checks, $failures failed"
[ "$failures" -eq 0 ]
