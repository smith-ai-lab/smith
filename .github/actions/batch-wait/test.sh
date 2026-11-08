#!/usr/bin/env bash
# Tests of wait.sh (#2851) against made-up queue branches: each call of the fake ls-remote reads the next line
# of a script of queue states, so a case says what the queue looks like at each poll.
set -u
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
fail=0
A=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
B=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
M=mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm
own=refs/heads/gh-readonly-queue/main/pr-1-$M   # entry 1, built on main M, head A
two=refs/heads/gh-readonly-queue/main/pr-2-$A   # entry 2, built on entry 1's head A, head B
three=refs/heads/gh-readonly-queue/main/pr-3-$B # entry 3, built on entry 2's head B
cat >"$tmp/ls" <<'SH'
#!/usr/bin/env bash
n=$(cat "$STATE.n" 2>/dev/null || echo 1)
line=$(sed -n "${n}p" "$STATE")
[ -z "$line" ] && line=$(tail -n 1 "$STATE")
echo $((n + 1)) >"$STATE.n"
[ "$line" = FAIL ] && exit 1
for r in $line; do [ "$r" = - ] || echo "0000000000000000000000000000000000000000	$r"; done
SH
chmod +x "$tmp/ls"
# case <name> <expected stdout> <expected exit> <state per poll>...
case_() {
  local name=$1 want=$2 want_rc=$3; shift 3
  printf '%s\n' "$@" >"$tmp/state"; rm -f "$tmp/state.n"
  local out rc=0
  out=$(STATE="$tmp/state" LS_REMOTE="$tmp/ls" REPO_URL=x OWN_REF="$own" HEAD_SHA=$A POLL=1 WAIT_CAP=${CAP:-60} \
    MISS_SLEEP=0 bash "$here/wait.sh" 2>>"$tmp/log") || rc=$?
  if [ "$out" = "$want" ] && [ "$rc" = "$want_rc" ]; then echo "ok   $name"
  else echo "FAIL $name: got '$out' exit $rc, want '$want' exit $want_rc"; fail=1; fi
}
# use case 3: alone in the queue, it builds at once, as today
case_ alone batch=alone 0 "$own"
# use case 1: a later group builds on it, then merges it (its branch goes): the run is cancelled, nothing built
case_ merged-by-later "" 3 "$own $two $three" "$own $two $three" "-" "-"
# use case 2: the later group failed and left: now last, it builds its own tree
case_ later-failed batch=waited 0 "$own $two" "$own $two" "$own"
# a later group further back (pr-3 on pr-2) does not make entry 1 wait by itself
case_ only-further-back batch=alone 0 "$own $three"
# one read that misses its own branch is not a removal
case_ one-miss batch=alone 0 "-" "$own"
# (a) the later group is named after the group's own head (A), not the PR's head (P): a branch on P is no later group
P=pppppppppppppppppppppppppppppppppppppppp
case_ pr-head-is-not-group-head batch=alone 0 "$own refs/heads/gh-readonly-queue/main/pr-2-$P"
# (b) a jump recreates every group branch: this run's branch is gone (twice), so it exits 3 and cancels itself,
# while the new groups (new shas) wait on nothing of this run; the new run starts from its own branch
J=jjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjj
case_ jump-recreates-refs "" 3 "$own $two" "refs/heads/gh-readonly-queue/main/pr-9-$M refs/heads/gh-readonly-queue/main/pr-1-$J"
# a failed read builds (costs runners, never safety)
case_ read-failed batch=waited 0 "$own $two" FAIL
# the wait is capped: it builds anyway
CAP=2 case_ capped batch=capped 0 "$own $two"
[ -n "${VERBOSE:-}" ] && cat "$tmp/log"
rm -rf "$tmp"
exit $fail
