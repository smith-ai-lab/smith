---
type: design
title: The runner ledger
description: Where the 20 hosted job slots go: runner-minutes by event, job and outcome over a window, and how to take it again.
tags: [ci, process]
---
# The runner ledger

The account runs at most 20 hosted jobs at once (`ubuntu-*` and `macos-*`; the owner's Mac is self-hosted and not counted) and cannot get more (#3069). The ledger says who spends them. It reads only the REST API (a cloud session cannot reach GraphQL) and caches every page in a scratch folder, so a second pass costs no calls: about 280 runs and 300 calls for 24 hours, out of 15000 an hour.

**What it counts.** A job's minutes are `completed_at - started_at` (wall minutes; billing rounds each job up to the minute), only for jobs that ran (`skipped` left out), clipped to the window. A queue run (`merge_group`) "merged" when its `head_sha` is a commit of main; a PR check (`ci` started by `workflow_dispatch`, from ci-dispatch) is "final head merged" when its `head_sha` is the last head of a merged PR. A job's wait is `started_at - created_at`: GitHub's own queue. A PR that ci-dispatch has not started yet holds no job and is not in it.

**The first ledger** (2026-11-10T02:25Z to 2026-11-11T02:25Z, in #3069's body): 3746 hosted runner-minutes, 15 PRs merged, **250 a merged PR**; the target of 10 merges an hour on 1200 runner-minutes an hour needs 120 or fewer. merge_group 53 %, PR checks 29 %, pushes to main 14 %. Queue runs that never merged took 26 % (half of the one saturated hour), the count 25 %, `warm` 11.5 %, the PR check's hosted macOS job 8.4 %, and the `setup-smith` step 34 % of every job's time. The mean was 2.6 hosted jobs running: the slots were full only from 01Z to 02:25Z on 2026-11-11, and GitHub's job wait had a median of 0 and a p90 of about 2 minutes, so the waits PRs see are ci-dispatch's.

**Taking it again.** Fetch into a fresh folder (rate limit: `gh api rate_limit --jq .resources.core`):
```
d=$(mktemp -d); mkdir -p "$d/runs" "$d/jobs" "$d/pulls"
since=2026-11-10T02:00:00Z   # the window's start
for p in $(seq 1 30); do
  gh api "repos/smith-ai-lab/smith/actions/runs?per_page=100&page=$p&created=>=$since" >"$d/runs/p$p.json"
  [ "$(jq '.workflow_runs|length' "$d/runs/p$p.json")" -lt 100 ] && break
done
jq -s '[.[].workflow_runs[]]' "$d"/runs/p*.json >"$d/runs.json"
for id in $(jq -r '.[].id' "$d/runs.json"); do   # one call a run, about 4 minutes for 276 runs: run it in the background
  [ -s "$d/jobs/$id.json" ] || gh api "repos/smith-ai-lab/smith/actions/runs/$id/jobs?filter=all&per_page=100" >"$d/jobs/$id.json"
done
for p in 1 2 3; do gh api "repos/smith-ai-lab/smith/pulls?state=closed&sort=updated&direction=desc&per_page=100&page=$p" >"$d/pulls/c$p.json"; done
gh api "repos/smith-ai-lab/smith/commits?sha=main&since=$since&per_page=100" >"$d/commits.json"
```
Then, with the three scripts below saved outside that folder, `python3 -I ledger.py "$d" <start> <end>` (by kind, event, outcome, job and step, and the job waits), `python3 -I conc.py "$d" <end>` (hosted jobs running and queued per minute, by hour) and `python3 -I queue.py "$d"` (each queue run: outcome, hosted minutes, its first failed job and the minutes spent after it). A window of one busy hour (`ledger.py "$d" 2026-11-11T00:55:00Z 2026-11-11T02:25:00Z`) shows the saturated regime; the whole day shows the cost per merged PR.

`ledger.py`:
```python
#!/usr/bin/env python3
# Runner-minute ledger for #3069. Reads cached API files from the folder given as argv[1].
import json, os, sys, glob, collections
from datetime import datetime, timezone

D = sys.argv[1]
T0 = sys.argv[2] if len(sys.argv) > 2 else "2026-11-10T02:25:00Z"
T1 = sys.argv[3] if len(sys.argv) > 3 else "2026-11-11T02:25:00Z"
def ts(s): return datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
t0, t1 = ts(T0), ts(T1)
runs = json.load(open(f"{D}/runs.json"))
pulls = []
for f in glob.glob(f"{D}/pulls/*.json"): pulls += json.load(open(f))
merged = [p for p in pulls if p.get("merged_at")]
merged_in = [p for p in merged if t0 <= ts(p["merged_at"]) < t1]
final_heads = {p["head"]["sha"] for p in merged}
main_shas = {c["sha"] for c in json.load(open(f"{D}/commits.json"))}

def hosted(labels):
    l = set(labels)
    if "self-hosted" in l: return None
    if any(x.startswith("macos") for x in l): return "mac"
    if any(x.startswith("ubuntu") for x in l): return "linux"
    return None

def mins(a, b):
    if not a or not b: return 0.0
    return max(0.0, (ts(b) - ts(a)).total_seconds() / 60)

rows = []  # (event, wf, outcome, job, kind, minutes, queue_wait)
steps = collections.Counter()
stepcount = collections.Counter()
for r in runs:
    p = f"{D}/jobs/{r['id']}.json"
    if not os.path.exists(p): continue
    try: jobs = json.load(open(p)).get("jobs", [])
    except ValueError: continue
    wf = r["name"].split(":")[0]
    ev = r["event"]
    if wf == "ci" and ev == "workflow_dispatch": ev = "pr_check"
    if wf == "ci" and ev == "merge_group":
        out = "merged" if r["head_sha"] in main_shas else ("ok_not_merged" if r["conclusion"] == "success" else f"not_merged:{r['conclusion']}")
    elif wf == "ci" and ev == "pr_check":
        out = "final_head_merged" if r["head_sha"] in final_heads else ("superseded_or_open" if r["conclusion"] in ("success",) else f"wasted:{r['conclusion']}")
    else:
        out = r["conclusion"] or r["status"]
    for j in jobs:
        k = hosted(j.get("labels") or [])
        if not k: continue
        a, b = j.get("started_at"), j.get("completed_at")
        if not b: b = T1
        if not a or ts(a) >= t1 or ts(b) < t0: continue
        m = mins(max(a, T0), min(b, T1))
        if j.get("conclusion") == "skipped": continue
        qw = mins(j.get("created_at"), a)
        rows.append((ev, wf, out, j["name"].split(" (")[0], k, m, qw, r["id"]))
        for s in j.get("steps") or []:
            sm = mins(s.get("started_at", "").split(".")[0].rstrip("Z") + "Z" if s.get("started_at") else None,
                      s.get("completed_at", "").split(".")[0].rstrip("Z") + "Z" if s.get("completed_at") else None)
            steps[(j["name"].split(" (")[0], s["name"])] += sm
            stepcount[(j["name"].split(" (")[0], s["name"])] += 1

def table(key, title, n=40):
    c = collections.defaultdict(float); cnt = collections.Counter()
    for row in rows:
        kk = key(row); c[kk] += row[5]; cnt[kk] += 1
    tot = sum(c.values())
    print(f"\n## {title} (total {tot:.0f} hosted runner-min)")
    for kk, v in sorted(c.items(), key=lambda x: -x[1])[:n]:
        print(f"{v:8.0f}  {100*v/tot:5.1f}%  jobs={cnt[kk]:4d}  {kk}")

print(f"window {T0} .. {T1}; merged PRs in window: {len(merged_in)}; main commits: {len(main_shas)}")
table(lambda r: (r[4],), "by runner kind")
table(lambda r: (r[0], r[1]), "by event/workflow")
table(lambda r: (r[0], r[2]), "by event/outcome")
table(lambda r: (r[0], r[3]), "by event/job", 60)
# queue wait
qw = collections.defaultdict(list)
for r in rows: qw[(r[0], r[4])].append(r[6])
print("\n## wait for a runner (created->started), minutes: median / p90 / n")
for k, v in sorted(qw.items()):
    v = sorted(v); print(k, f"{v[len(v)//2]:.1f} / {v[int(len(v)*0.9)]:.1f} / {len(v)}")
print("\n## top steps by summed minutes")
for k, v in steps.most_common(45):
    print(f"{v:8.0f}  n={stepcount[k]:4d}  {k[0]} :: {k[1]}")
```
`conc.py`:
```python
#!/usr/bin/env python3
# Concurrency of hosted jobs per minute, from the cached jobs (argv[1] = folder).
import json, os, sys, glob, collections
from datetime import datetime, timezone, timedelta
D = sys.argv[1]
def ts(s): return datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
run_ev = {r["id"]: r["event"] for r in json.load(open(f"{D}/runs.json"))}
now = ts(sys.argv[2]) if len(sys.argv) > 2 else datetime.now(timezone.utc).replace(microsecond=0)
running = collections.Counter(); queued = collections.Counter(); byev = collections.defaultdict(collections.Counter)
for f in glob.glob(f"{D}/jobs/*.json"):
    try: jobs = json.load(open(f)).get("jobs", [])
    except ValueError: continue
    for j in jobs:
        l = set(j.get("labels") or [])
        if "self-hosted" in l or not any(x.startswith(("ubuntu", "macos")) for x in l): continue
        if j.get("conclusion") == "skipped": continue
        c, a, b = j.get("created_at"), j.get("started_at"), j.get("completed_at")
        if not a: continue
        A = ts(a); B = ts(b) if b else now; C = ts(c) if c else A
        ev = run_ev.get(j["run_id"], "?")
        up = lambda x: x.replace(second=0) + (timedelta(minutes=1) if x.second else timedelta(0))
        t = up(C)
        while t < A:
            queued[t] += 1; t += timedelta(minutes=1)
        t = up(A)
        while t < B:
            running[t] += 1; byev[t][ev] += 1; t += timedelta(minutes=1)
hours = collections.defaultdict(list)
for t in sorted(set(running) | set(queued)):
    hours[t.strftime("%m-%dT%H")].append((running[t], queued[t]))
print("hour      mean_run max_run mean_queued max_queued minutes>=18running")
for h, v in sorted(hours.items()):
    r = [x[0] for x in v]; q = [x[1] for x in v]
    print(f"{h}  {sum(r)/60:6.1f} {max(r):6d} {sum(q)/60:8.1f} {max(q):6d} {sum(1 for x in r if x>=18):5d}")
top = sorted(running, key=lambda t: -running[t])[:5]
for t in top: print(t, running[t], dict(byev[t]))
```
`queue.py`:
```python
#!/usr/bin/env python3
# Per merge_group run: outcome, hosted minutes, first failed job, minutes spent after the first failure.
import json, sys, re
from datetime import datetime, timezone
D = sys.argv[1]
def ts(s): return datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
main = {c["sha"] for c in json.load(open(f"{D}/commits.json"))}
tot = {"after_fail": 0.0, "all": 0.0}
for r in sorted(json.load(open(f"{D}/runs.json")), key=lambda r: r["created_at"]):
    if r["event"] != "merge_group" or r["name"] != "ci": continue
    jobs = json.load(open(f"{D}/jobs/{r['id']}.json"))["jobs"]
    hj = [j for j in jobs if j.get("started_at") and "self-hosted" not in (j.get("labels") or [])
          and any(x.startswith(("ubuntu", "macos")) for x in j.get("labels") or []) and j["conclusion"] != "skipped"]
    end = lambda j: ts(j["completed_at"]) if j.get("completed_at") else ts("2026-11-11T02:30:00Z")
    m = sum((end(j) - ts(j["started_at"])).total_seconds() / 60 for j in hj)
    fails = sorted([j for j in jobs if j["conclusion"] == "failure" and j["name"] != "ci"], key=end)
    after = 0.0; first = "-"
    if fails:
        f0 = end(fails[0]); first = f"{fails[0]['name'][:24]}@{(f0 - ts(r['run_started_at'])).total_seconds()/60:.0f}m"
        after = sum(max(0, (end(j) - max(f0, ts(j["started_at"]))).total_seconds() / 60) for j in hj)
    pr = re.search(r"pr-(\d+)", r["head_branch"]).group(1)
    out = "MERGED" if r["head_sha"] in main else (r["conclusion"] or "running")
    dur = (ts(r["updated_at"]) - ts(r["run_started_at"])).total_seconds() / 60
    tot["after_fail"] += after; tot["all"] += m
    print(f"{r['created_at'][5:16]} pr{pr} {out:9s} wall={dur:4.0f} hosted={m:5.0f} first_fail={first:32s} after_fail={after:4.0f}")
print(tot)
```
