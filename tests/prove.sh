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
# The proofs are cached by content and by the prover's identity (shim/prove.cpp), so the proofs of these programs are kept
# between runs, in the tests' own cache (never the real one): a run with an unchanged prover reuses them, and `smith check`
# spends its CPU on what changed (issue #31). The cases that test the cache or a cold proof (15, 17) use a cache of their
# own, and a proof that fails is never cached.
proof_cache=${SMITH_TEST_CACHE:-$HOME/.cache/smith-tests}/proofs
mkdir -p "$proof_cache" "$XDG_CACHE_HOME/smith" && ln -s "$proof_cache" "$XDG_CACHE_HOME/smith/proofs"
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

# 1b. smith_assert is a proof point, like the assert it replaces.
prove "$cases/assertion.c" --function=next
expect "assert: one the premise gives is proved" 0 "$rc"
prove "$cases/assertion.c" --function=too_sure
expect "assert: one it does not give fails" 1 "$rc"
expect_contains "assert: ... with its line and condition" "$out" "assertion.c:16: assert(x < 50) may fail"

# 2. A caller that breaks the contract fails, with the line and the values.
prove "$cases/clamp.c" "$cases/bad_caller.c"
expect "caller: fails" 1 "$rc"
expect_contains "caller: ... at the call, with the contract it breaks" "$out" "bad_caller.c:10: the call of clamp may break smith_requires(low <= high)"
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
expect_contains "assembly: ... and says to mark the function trusted" "$out" "put smith_trusted() as the first statement of sensor_percent"

# 6. Each construct that cannot be proved is an error that says where and what to do.
prove "$cases/recursion.c"
expect_contains "recursion: is an error" "$out" "recursion.c:9: cannot be proved: recursion: countdown calls itself"
expect_contains "recursion: ... that says what to do" "$out" "write it as a loop with a constant bound"
prove "$cases/pointer.c"
expect_contains "function pointer: is an error" "$out" "pointer.c:10: cannot be proved: a call through a function pointer"
prove "$cases/nobody.c"
expect_contains "no body: is an error" "$out" "nobody.c:10: cannot be proved: a call of lookup, which has neither a body nor a contract"
expect_contains "no body: ... that suggests a trusted boundary" "$out" "this is the edge of the program: put smith_trusted() as the first statement of"
prove "$cases/unbounded.c"
expect_contains "loop: with no bound is an error" "$out" 'unbounded.c:8: cannot be proved: the loop has no bound the prover can see: its exit depends on `n`'
expect_contains "loop: ... and says to bound it with a requires" "$out" "contract::requires(n <= ...)"
prove "$cases/loop_count.c" --function=sum_unbounded
expect "loop count: no bound fails" 1 "$rc"
expect_contains "loop count: names the line and the count" "$out" 'loop_count.c:11: cannot be proved: the loop has no bound the prover can see: it runs `count` times'
expect_contains "loop count: ... and the requires to write" "$out" "state its worst case, contract::requires(count <= ...), which callers then prove"
prove "$cases/loop_count.c" --function=sum_bounded
expect "loop count: a requires on the count bounds the loop" 0 "$rc"
prove "$cases/loop_count.c" --function=digits_divided
expect_contains "loop count: a divide has no count, and the passes it takes are said" "$out" "a 64-bit value divided by 10 each pass is not a count LLVM can derive"
expect_contains "loop count: ... with the counter that bounds it" "$out" "at most 20"
prove "$cases/loop_count.c" --function=digits_counted
expect "loop count: a counter bounded by a constant proves" 0 "$rc"
prove "$cases/message.m"
expect_contains "Objective-C: a message send is an error" "$out" "message.m:10: cannot be proved: an Objective-C message send"

# 6b. A proof that grows past its memory limit is stopped, and says so (macOS enforces no RLIMIT_AS, so the parent watches the
# process's footprint). This one uses about 0.9 GB in its 10 s; 0.3 GB stops it first.
prove --memory=0.3 "$cases/blowup.c"
expect "memory: a proof over its limit fails" 1 "$rc"
expect_contains "memory: ... naming the limit" "$out" "over its memory limit of 0.3 GB"
expect_contains "memory: ... and the function" "$out" "FAIL  blowup"
expect_contains "memory: ... and the way out" "$out" "--memory=GB changes the limit"

# 7. A proof that runs out of time fails, like a test that times out. The budget is CPU time (so that a busy machine does
# not change a verdict), counted as on 8 idle cores: this one query runs on one thread, and 2 s of it is the whole budget.
prove --budget=2 "$cases/division.c"
expect "budget: fails" 1 "$rc"
expect_contains "budget: ... as a budget of CPU time" "$out" "over its budget of 2 s of CPU time"
expect_contains "budget: ... that says what it used" "$out" "having used 2."
expect_contains "budget: ... which query was slow, with its line" "$out" "CPU  smith_ensures(q * y + r == x)  ("
expect_contains "budget: ... and that it was the one the budget ended in" "$out" "<- the budget ended here"
expect_contains "budget: ... and the division it depends on, with its line" "$out" "its value depends on a 64-bit unsigned division by a variable at "
expect_contains "budget: ... and the remainder" "$out" "64-bit unsigned remainder by a variable"
expect_contains "budget: ... and the whole list of costly operations of the code" "$out" "division.c:8: 64-bit unsigned division by a variable"
expect_contains "budget: ... and where the time went" "$out" "where the time went (CPU, as charged):"

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
# A leftover is never killed here: once its parent is gone it is launchd's, and its pid can already be another
# process's (another test's compile). A proof also ends by its own deadline (its alarm), so it cannot run on for ever.

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

# 7c2. A const pointer is not written by a call, and a callee with many blocks is used through its contract.
for function in add_if_set turn_after spread spread_then_check; do
    prove --function=$function "$cases/readonly.c"
    expect "readonly: $function is proved" 0 "$rc"
done
prove --function=turn_after_overlapping "$cases/readonly.c"
expect "readonly: a list that is not restrict may be the board, and the claim fails" 1 "$rc"
prove --function=grows_by_one "$cases/readonly.c"
expect "readonly: a claim stronger than the contract fails" 1 "$rc"
expect_contains "readonly: ... with its line" "$out" "readonly.c:54: smith_ensures(now == old + 1) may be false"

# 7c2b. What a caller reads after a callee with UBSan checks and a contract is a value, not undefined behaviour (#62).
prove --function=add_row "$cases/after_call.c"
expect "after_call: add_row is proved" 0 "$rc"
prove --function=replay_make --library="$here/bootstrap/lib/smith/arena.c" "$cases/arena_sret.c"
expect "after_call: a store through sret after a trusted call with a writable pointer is proved (#74)" 0 "$rc"

# 7c2c. A pointer returned under an smith_ensures is valid or null, proved at the return and assumed by callers (#74).
prove --function=offset_in_kind "$cases/returned_pointer.c"
expect "returned_pointer: a load through a returned pointer is proved with ensures(result != nullptr)" 0 "$rc"
prove --function=past_the_end "$cases/returned_pointer.c"
expect "returned_pointer: a pointer one past the end is refused where it is returned" 1 "$rc"
expect_contains "returned_pointer: ... at its return" "$out" "undefined behaviour at \`ret ptr"

# 7c3. Two pointer parameters may be the same object unless the source says otherwise (issue #57): no claim rests on
# int32_t * and uint32_t *, a struct and a pointer to its member, or two struct types being different objects; `restrict`
# is what says it.
for function in same_type signed_unsigned member other_struct; do
    prove --function=$function "$cases/alias.c"
    expect "alias: $function fails, since its pointers may overlap" 1 "$rc"
    expect_contains "alias: $function ... on the claim that rests on it" "$out" "smith_ensures("
done

for function in restricted restricted_other_type reads_both; do
    prove --function=$function "$cases/alias.c"
    expect "alias: $function is proved, restrict being what says they are different" 0 "$rc"
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

# 7d2. A struct returned through sret is translated (issue #74): the function, its caller, and a wrong promise.
for function in make_wide use_wide wide_from; do
    prove --function=$function "$cases/sret.c"
    expect "sret: $function is proved" 0 "$rc"
done
prove --function=wrong_wide "$cases/sret.c"
expect "sret: a wrong promise about the result fails" 1 "$rc"
expect_contains "sret: ... as a promise that may be false" "$out" "sret.c:34: smith_ensures(wide.b == 1) may be false"

# 7e. A file that does not compile is an error with clang's message, never "nothing to prove".
printf '#include <smith/contract.h>\nint f(int x) { return x +; }\n' >"$work/syntax_error.c" # not a .c file of the repository: the tree-sitter test would call it a parse error
prove "$work/syntax_error.c"
expect "compile error: fails" 1 "$rc"
expect_contains "compile error: ... with clang's message" "$out" "syntax_error.c:2:26: error: expected expression"
case $out in *"nothing to prove"*) expect "compile error: is not nothing to prove" "an error" "$out" ;; *) expect "compile error: is not nothing to prove" "an error" "an error" ;; esac
prove "$cases/memory.c" "$work/syntax_error.c"
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

    # 11b. The project verb proves .smith sources too (issue #42): a convention project, no build file, with a contract in
    # a module and a trusted function written with c::smith_trusted().
    proj=$work/smithproj
    mkdir -p "$proj"
    cat >"$proj/main.smith" <<'EOF'
import primes;
import <stdio.h>;

int main(void) {
    c::printf("%d\n", primes::shown_sum(3));
    return 0;
}
EOF
    cat >"$proj/primes.smith" <<'EOF'
module primes;

import <stdio.h>;
import <smith/contract.h>;

export i32 show(i32 value) {
    c::smith_trusted();
    c::smith_requires(value >= 0);
    c::printf("%d\n", value);
    c::smith_ensures(value >= 0);
    return value;
}

export i32 sum(usize count) {
    constexpr i32 first[] = {2, 3, 5, 7, 11};
    c::smith_requires(count <= _Countof(first));
    i32 total = 0;
    for (usize i = 0; i < count; i++) {
        total += first[i];
    }
    c::smith_ensures(total >= 0 && total <= 28);
    return total;
}

export i32 shown_sum(usize count) {
    c::smith_requires(count <= 5);
    i32 total = sum(count);
    i32 shown = show(total);
    c::smith_ensures(shown >= 0);
    return shown;
}
EOF
    cd "$proj" && capture "$smith" prove -v --emit-ir="$work/project-ir.ll"; cd "$here"
    expect "project: a .smith project's contracts are proved" 0 "$rc"
    expect_contains "project: ... as written" "$out" "ok    primes::sum  (primes.smith:14)"
    expect_contains "project: ... through a trusted function's contract" "$out" "ok    primes::shown_sum  ("
    expect_contains "project: ... the trusted function is listed" "$out" "trusted  primes::show  (primes.smith:6)"
    expect_contains "project: ... with the counts" "$out" "2 proved, 0 failed, 1 trusted"
    case $out in *"nothing to prove"*) expect "project: a .smith project is not nothing to prove" "proved" "$out" ;; *) expect "project: a .smith project is not nothing to prove" "proved" "proved" ;; esac
    ir=$(cat "$work/project-ir.ll")
    expect_contains "project: --emit-ir writes the IR of every function" "$ir" "; primes::shown_sum ("
    expect_contains "project: ... and of the first" "$ir" "; primes::sum ("
    sed 's/total <= 28/total <= 27/' "$proj/primes.smith" >"$proj/primes.next" && mv "$proj/primes.next" "$proj/primes.smith"
    cd "$proj" && capture "$smith" prove; cd "$here"
    expect "project: a wrong contract in a .smith module is refuted" 1 "$rc"
    expect_contains "project: ... at the .smith line, with the values" "$out" "primes.smith:21: smith_ensures(total >= 0 && total <= 27) may be false"
    expect_contains "project: ... counterexample" "$out" "counterexample: count = 5"
    cd "$proj" && capture "$smith" prove --budget=30 -v; cd "$here"
    expect_contains "project: the prover's options are the project's too (-v, --budget=)" "$out" "ok    primes::shown_sum"
    # 11c. A project's proof reads smith's libraries for their contracts (issue #66): a proved function may call a library
    # export, whatever the library marks it with, and is proved against the library's contract. The library's own
    # functions are not proved there.
    libs=$work/libsproj
    mkdir -p "$libs"
    cat >"$libs/main.smith" <<'EOF'
import libs;
import <stdio.h>;

int main(void) {
    c::printf("%d\n", (int)libs::pick(2));
    return 0;
}
EOF
    cat >"$libs/libs.smith" <<'EOF'
module libs;

import smith::contract;
import smith::random;

export u64 pick(u64 index) {
    contract::requires(index < 10);
    random::Key key = random::key_make(7);
    random::Key part = random::key_derive(key, index);
    contract::ensures(true);
    return part.bits;
}
EOF
    cd "$libs" && capture "$smith" prove -v; cd "$here"
    expect "libraries: a proved function that calls a library export is proved" 0 "$rc"
    expect_contains "libraries: ... against the library's contract" "$out" "ok    libs::pick  ("
    expect_contains "libraries: ... and only the project's functions are counted" "$out" "1 proved, 0 failed"
    case $out in *"neither a body nor a contract"*) expect "libraries: no 'neither a body nor a contract' for a library function" "none" "$out" ;; *) expect "libraries: no 'neither a body nor a contract' for a library function" "none" "none" ;; esac
    case $out in *"random::"*) expect "libraries: the library's own functions are not proved or listed" "none" "$out" ;; *) expect "libraries: the library's own functions are not proved or listed" "none" "none" ;; esac
else
    echo "skipped: this smith's clang has no token hook, so no .smith proofs"
fi


# 12. Trusted boundaries (issue #42): a function marked smith_trusted() is assumed by its callers and not proved, and the
# report lists it as trusted. The default set is every function with a contract, minus the trusted ones: main and a
# function with no contract are not proved.
prove -v "$cases/trusted.c"
expect "trusted: a proved function that is wrong still fails" 1 "$rc"
expect_contains "trusted: ... the edge function is listed as trusted" "$out" "trusted  sensor_percent  ("
expect_contains "trusted: ... with the contract it gives callers" "$out" "not proved; callers prove its 0 smith_requires and assume its 1 smith_ensures"
expect_contains "trusted: ... one with no contract at all too" "$out" "trusted  announce  ("
expect_contains "trusted: ... the callers are proved from its contract" "$out" "ok    scaled  ("
expect_contains "trusted: ... the one that wants more than the contract gives fails" "$out" "FAIL  scaled_too_far  ("
expect_contains "trusted: ... with the value the contract allows" "$out" "percent = "
expect_contains "trusted: ... and the counts say so" "$out" "3 proved, 1 failed, 2 trusted"
case $out in *"inline assembly"*) expect "trusted: the body of a trusted function is not read" "no error" "$out" ;; *) expect "trusted: the body of a trusted function is not read" "no error" "no error" ;; esac
case $out in *"ok    sensor_percent"* | *"ok    announce"*) expect "trusted: a trusted function is never listed as proved" "not proved" "$out" ;; *) expect "trusted: a trusted function is never listed as proved" "not proved" "not proved" ;; esac
case $out in *"main"* | *"ok    helper"* | *"FAIL  helper"*) expect "default set: main and a function with no contract are not proved" "neither" "$out" ;; *) expect "default set: main and a function with no contract are not proved" "neither" "neither" ;; esac
prove --function=branches_on_trusted "$cases/trusted.c"
expect "trusted: a caller may branch on the result" 0 "$rc"
prove --function=after_announce "$cases/trusted.c"
expect "trusted: a caller of a trusted function with no contract is proved, reading the function with no contract" 0 "$rc"
prove --function=sensor_percent "$cases/trusted.c"
expect "trusted: --function of a trusted function is refused" 1 "$rc"
expect_contains "trusted: ... saying it is assumed, not proved" "$out" "the function is marked smith_trusted(), so its contract is assumed and nothing of it is proved"
prove --function=helper "$cases/trusted.c"
expect "default set: --function of a function with no contract is refused" 1 "$rc"
expect_contains "default set: ... saying it has no contract" "$out" "a function with no contract is not proved"
prove "$cases/edge.c"
expect "boundary: a proved function that reaches a library call fails" 1 "$rc"
expect_contains "boundary: ... naming the call" "$out" "edge.c:9: cannot be proved: a call of printf, which has neither a body nor a contract"
expect_contains "boundary: ... and the function to mark, the helper that makes the call" "$out" "put smith_trusted() as the first statement of show, so that its callers use its smith_requires and smith_ensures"
prove "$cases/edge_trusted.c"
expect "boundary: marking the helper trusted cuts it, and the callers are proved from its contract" 1 "$rc"
expect_contains "boundary: ... the one that passes what the contract refuses fails at the call" "$out" "edge_trusted.c:25: the call of show may break smith_requires(value >= 0)"
expect_contains "boundary: ... the other is proved, the helper is trusted" "$out" "1 proved, 1 failed, 1 trusted"
prove --function=shown_twice "$cases/edge_trusted.c"
expect "boundary: ... shown_twice alone is proved" 0 "$rc"

# 13. An error at its cause (issue #42): a smith_requires after a write names the write and the read, each with its line.
prove "$cases/cause.c"
expect "cause: callers of a contract that reads after a write fail" 1 "$rc"
expect_contains "cause: ... the error is at the call" "$out" "cause.c:22: cannot be proved: cannot use the contract of swap_halves: its smith_requires reads memory at "
expect_contains "cause: ... names the read, with its line and instruction" "$out" "cause.c:15: \`"
expect_contains "cause: ... and the write, with its line and instruction" "$out" "after the function has written, at "
expect_contains "cause: ... the store" "$out" "cause.c:14: \`store i32"
expect_contains "cause: ... and what to do" "$out" "put every smith_requires before the first write"
prove "$cases/zero_init.c"
expect "cause: a struct zero-initialized first is a write of a local, not of the caller's memory" 0 "$rc"

# 14. --emit-ir writes the IR the prover checks, with the proof points listed.
prove --emit-ir="$work/ir.ll" --function=draw_health "$cases/clamp.c"
expect "emit-ir: the proof still runs" 0 "$rc"
ir=$(cat "$work/ir.ll")
expect_contains "emit-ir: names the function" "$ir" "; draw_health ("
expect_contains "emit-ir: lists the proof points" "$ir" ";   point 0: smith_requires, assumed: 0 <= max_health && max_health <= 1000"
expect_contains "emit-ir: ... the one that proves a callee's smith_requires at the call" "$ir" "smith_requires of a callee, proved at the call: low <= high"
expect_contains "emit-ir: ... and the promise" "$ir" "smith_ensures, proved: 0 <= pixels && pixels <= 4 * max_health"
expect_contains "emit-ir: has the function" "$ir" "define "
expect_contains "emit-ir: ... with the contracts as points" "$ir" "call void @smith_point(i32 "

# 15. -v lists every function, and a function proved before is cached, with the time it took then.
export XDG_CACHE_HOME=$work/xdg-proofs
prove -v "$cases/clamp.c"
expect_contains "cached: the first run proves" "$out" "ok    clamp  ("
case $out in *cached*) expect "cached: the first run is not cached" "no" "$out" ;; *) expect "cached: the first run is not cached" "no" "no" ;; esac
prove -v "$cases/clamp.c"
expect_contains "cached: the second run lists clamp, marked cached" "$out" "ok    clamp  ("
expect_contains "cached: ... with the time it took when it was proved" "$out" "cached: proved before in "
expect_contains "cached: ... and draw_health, which uses its contract" "$out" "ok    draw_health  ("
expect_contains "cached: ... and the count" "$out" "2 proved, 0 failed, 2 cached"
prove -v --no-cache "$cases/clamp.c"
case $out in *cached*) expect "cached: --no-cache proves again" "no" "$out" ;; *) expect "cached: --no-cache proves again" "no" "no" ;; esac
prove "$cases/bad_caller.c" "$cases/clamp.c"
expect_contains "cached: a function that fails is never cached" "$out" "bad_caller.c:10: the call of clamp may break smith_requires(low <= high)"
export XDG_CACHE_HOME=$work/xdg-cache

# 16. A function with a smith_assert is in the default set (issue #53), as one with requires, ensures or invariant is: main
# included. A program with nothing of the four has nothing to prove, and is not flattened for it.
prove -v "$cases/asserts_only.c"
expect "asserts only: the function whose assert follows is proved, the other one fails" 1 "$rc"
expect_contains "asserts only: a function with only an assert is a unit" "$out" "ok    masked  ("
expect_contains "asserts only: ... one whose assert is not given fails, with its line" "$out" "asserts_only.c:14: assert(x < 1000) may fail"
case $out in *"ok    main"* | *"FAIL  main"*) expect "asserts only: main has no assert of its own and is not proved" "no" "$out" ;; *) expect "asserts only: main has no assert of its own and is not proved" "no" "no" ;; esac
prove "$cases/asserts_only.c" --function=masked
expect "asserts only: --function=masked proves it" 0 "$rc"
printf 'int main(void) { return 0; }\n' >"$work/nothing.c"
prove -v "$work/nothing.c"
expect "gate: no contract and no assert is nothing to prove" 0 "$rc"
expect_contains "gate: nothing to prove" "$out" "smith: prove: nothing to prove: no function with smith_requires"
expect_contains "gate: -v counts the functions" "$out" "phase gate: 0 functions with a contract of their own, 0 trusted; skipped flattening"
case $out in *"phase flatten"*) expect "gate: no flattening" "no" "$out" ;; *) expect "gate: no flattening" "no" "no" ;; esac
prove "$work/nothing.c"
expect_contains "gate: the default output is the same without -v" "$out" "smith: prove: nothing to prove"
case $out in *phase*) expect "gate: no phase lines without -v" "no" "$out" ;; *) expect "gate: no phase lines without -v" "no" "no" ;; esac
# 16b. An assert in a function that calls the program's edge: the edge is a declaration marked smith::edge.
prove "$cases/main_edge.c" --function=run_proved
expect "edge: an assert is proved across a call of a smith::edge function" 0 "$rc"
prove "$cases/main_edge.c" --function=run_unproved
expect "edge: ... and one the contract does not give fails" 1 "$rc"
expect_contains "edge: ... with its line" "$out" "main_edge.c:33: assert(total <= 24) may fail"
prove -v "$cases/assertion.c"
expect_contains "gate: a contract is flattened" "$out" "phase flatten: "
expect_contains "gate: ... and the module's size before and after" "$out" " instructions before, "
expect_contains "gate: ... counting its functions" "$out" "phase gate: 2 functions with a contract of their own"
expect_contains "gate: ... a failing assert is still refuted" "$out" "assertion.c:16: assert(x < 50) may fail"

# 16c. The library that defines the contract functions (bootstrap/lib/smith/contract.c) in the program is no reason to
# inline them: the contracts stay points, and the function is still a unit. Only what a unit reaches is flattened.
prove "$cases/clamp.c" "$here/bootstrap/lib/smith/contract.c"
expect "contract.c in the program: the contracts are still proved" 0 "$rc"
expect_contains "contract.c in the program: ... as before" "$out" "proved, 0 failed"

# 17. The cache key is the prover's identity (LLVM's version and a version kept in prove.cpp), never the build time: a
# rebuild of smith with the same prover keeps the proofs.
if grep -q '__DATE__\|__TIME__' "$here/shim/prove.cpp"; then
    expect "cache key: no build time in it" "no" "yes"
else
    expect "cache key: no build time in it" "no" "no"
fi

echo
echo "$total prove checks, $failures failed"
[ "$failures" -eq 0 ]
