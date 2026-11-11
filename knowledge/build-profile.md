---
type: design
title: Profiling smith's build and tests, and what the profile of 2026-11-11 found (#3076)
description: How to re-run the profile (the executor's step log for a build, a /proc sampler that splits a test file's CPU by kind of process, the unsanitized runner), the numbers of 2026-11-11 on a 4-core container, and the cause found: SHA-256 of the 437 MB smith binary.
tags: [build, tests, performance]
---
# Profiling smith's build and tests

Measured on 2026-11-11 for #3076 on one cloud container (4 cores, 15 GB, Linux 6.18, main `7d1941a`, the dev profile, the newest release first on `PATH`). The earlier profile of the build alone is [build-times](build-times.md); the ranked plan and every table are in the body of #3076. This page is how to take the numbers again.

## The build
- **Cold:** remove `build/` and everything in `.smith/` except `stage0/`, then `SMITH_STEP_CACHE=0 smith build` under the shell's `time` (the container has no `/usr/bin/time`). Its clock drifts: take durations from the tools, never from two `date` calls.
- **The split by step** is the executor's log `.smith/.smith_log`: start ms, end ms, mtime, hash and path for each output. Outputs with the same start and end are one step. A copy of the log taken before a build, compared with the log after it, gives the steps that build ran.
- **An edit:** append one comment line to the file, `smith build`, twice, then `git checkout` the file. Keep the release first on `PATH`, never `build/<OS>-<arch>/` (the skill smith-dev-build).
- **Time inside the build tool that no step accounts for:** `strace -f -ttt -T -e trace=read,openat,execve smith build` shows what it reads between steps. That is how the hash of the binary was found: 878 MB read from `smith-cc` in two passes of about 5 s after a link of 2.2 s.
- **A fresh cache folder:** `XDG_CACHE_HOME=$(mktemp -d) smith build` in a one-file project, against the same with `steps/self/` copied in from a warm folder, measures the digest smith takes of itself (`runtime/self_digest.c`).

## The tests
- **The suite's CPU per test** is CI's own table, `.github/shard-costs.tsv`. Sort it before running anything.
- **One test file alone:** `smith test --no-cache --stats -v --test '<file>::'` with the smith under test first on `PATH`.
- **Where a file's CPU goes** needs the processes it starts. `strace -f` cannot be used on tests: LeakSanitizer refuses to run under ptrace and the tests fail. Sample `/proc` instead: the script below polls every 20 ms, keeps each process's own user and system time, and classes it by its command line (`smith build`, `smith cc -c`, lld, the solver's forks, the test's programs). The CPU of processes too short to be seen is the difference to the total. `python3 psample.py out.txt -- smith test --no-cache --test 'cache_modules_test::'`.
- **Sanitizers:** copy `tests/units/` to a scratch folder, remove `-fsanitize=address,undefined` from its `build.c`, build that runner and run the same in-process tests with both runners, twice each.
- **Comparing two smiths:** copy each binary to a scratch path first, build both the same way, run each side twice (the skill smith-dev-rules, Timing a change).

```python
#!/usr/bin/env python3
"""CPU of a command's process tree by class of process, without ptrace (the sanitizers' leak check refuses ptrace).

usage: psample.py OUT -- command...
Polls /proc every 20 ms: for each pid its user+system ticks (its own, not its children's), its command line and its
parent. A process is classed by its last command line; a fork that never execs is classed as `fork of <parent class>`.
The CPU of processes too short to be sampled is the difference to the command's total (RUSAGE_CHILDREN).
"""
import os, sys, time, subprocess, resource, collections, re

out = sys.argv[1]; cmd = sys.argv[3:]
tick = os.sysconf('SC_CLK_TCK')
me = os.getpid()
seen = {}  # (pid, start) -> [cpu ticks, cmdline, ppid, first, last]

def classify(words):
    if not words: return '?'
    base = words[0].rsplit('/', 1)[-1]
    if base in ('smith', 'smith-cc', 'smith-c++'):
        verb = words[1] if len(words) > 1 else ''
        if base != 'smith' or verb in ('cc', 'c++'):
            if '-cc1' in words: return 'smith cc1'
            if '--smith-header' in words or '--smith-headers' in words: return 'smith cc --smith-header(s)'
            if '--front-end=new' in words: return 'smith cc --front-end=new'
            if '-c' in words or '-fsyntax-only' in words or '-E' in words: return 'smith cc -c / -fsyntax-only'
            return 'smith cc (compile+link or link)'
        return 'smith ' + verb
    if base in ('ld.lld', 'lld', 'ld'): return 'lld'
    if base in ('sh', 'bash', 'dash'): return 'sh'
    if base == 'tests': return 'test runner (tests)'
    if base in ('git', 'nvim', 'clang', 'clangd', 'ninja', 'z3', 'python3', 'strace'): return base
    if base in ('cp', 'rm', 'mkdir', 'cat', 'ln', 'grep', 'sed', 'env', 'find', 'diff', 'cmp', 'touch', 'mv', 'ls',
                'head', 'tail', 'sort', 'wc', 'tr', 'chmod', 'tar', 'sleep', 'awk', 'mktemp', 'xargs', 'date'):
        return 'coreutils'
    return 'test programs (other executables)'

def poll(now):
    for name in os.listdir('/proc'):
        if not name.isdigit(): continue
        pid = int(name)
        if pid == me: continue
        try:
            with open(f'/proc/{name}/stat', 'rb') as f: s = f.read().decode('latin1')
            r = s.rfind(')'); f2 = s[r + 2:].split()
            ppid = int(f2[1]); cpu = int(f2[11]) + int(f2[12]); start = f2[19]
            key = (pid, start)
            e = seen.get(key)
            if e is None or e[0] != cpu or e[1] is None:
                with open(f'/proc/{name}/cmdline', 'rb') as f: c = f.read().decode('latin1').split('\0')[:-1]
            else:
                c = e[1]
            if e is None: seen[key] = [cpu, c, ppid, now, now]
            else: e[0] = cpu; e[1] = c or e[1]; e[4] = now
        except (FileNotFoundError, ProcessLookupError, IndexError, ValueError, PermissionError):
            pass

before = set()
poll(time.time()); before = set(seen); base_cpu = {k: v[0] for k, v in seen.items()}
r0 = resource.getrusage(resource.RUSAGE_CHILDREN); t0 = time.time()
p = subprocess.Popen(cmd)
while p.poll() is None:
    poll(time.time()); time.sleep(0.02)
wall = time.time() - t0
r1 = resource.getrusage(resource.RUSAGE_CHILDREN)
total = (r1.ru_utime - r0.ru_utime) + (r1.ru_stime - r0.ru_stime)

# keep the descendants of the command
bypid = {}
for (pid, start), v in seen.items(): bypid.setdefault(pid, []).append(((pid, start), v))
def is_desc(key, depth=0):
    v = seen[key]
    if key[0] == p.pid: return True
    if key in before or depth > 64: return False
    par = [k for k, _ in bypid.get(v[2], [])]
    return any(is_desc(k, depth + 1) for k in par)
cls = collections.defaultdict(lambda: [0, 0.0])
exe_of = {}
def klass(key, depth=0):
    v = seen[key]
    par = [k for k, _ in bypid.get(v[2], [])]
    c = classify(v[1])
    # a fork that never execs has its parent's command line
    if par and depth < 8 and seen[par[0]][1] == v[1] and par[0] not in before:
        return 'fork of ' + c if not c.startswith('fork of') else c
    return c
for key, v in seen.items():
    if key in before or not is_desc(key): continue
    k = klass(key); cls[k][0] += 1; cls[k][1] += v[0] / tick
sampled = sum(v[1] for v in cls.values())
with open(out, 'w') as f:
    f.write(f'wall {wall:.1f} s  cpu(user+sys of the tree) {total:.1f} s  user {r1.ru_utime-r0.ru_utime:.1f} sys {r1.ru_stime-r0.ru_stime:.1f}  sampled {sampled:.1f} s  exit {p.returncode}\n')
    f.write(f'{"class":44} {"sampled n":>9} {"cpu s":>8} {"share":>6}\n')
    for k, (n, c) in sorted(cls.items(), key=lambda kv: -kv[1][1]):
        if c >= 0.05 or n > 20: f.write(f'{k:44} {n:9d} {c:8.1f} {100*c/max(total,1e-9):5.1f}%\n')
    f.write(f'{"(processes too short to sample)":44} {"":9} {max(0,total-sampled):8.1f} {100*max(0,total-sampled)/max(total,1e-9):5.1f}%\n')
sys.exit(p.returncode)
```

## What it found (2026-11-11)
| What | Measured |
|---|---|
| Cold build, step cache off | 89.4 s wall, 165 CPU s; the C++ shims are 47% of step time; the link is 2.2 s warm and 28 s on a cold page cache |
| One-file edit of smith | 12 to 14 s, of which compile at most 1.2 s, link 2.2 s, two hashes of the binary 9 s, "headers of the tests" 4.4 s |
| Test runners after smith changed | 118.9 s wall, 266 CPU s (572 objects) |
| Start of one smith process | 10 ms |
| The suite in CI | 2,614 CPU s, 4,451 tests; 63 tests of 10 s or more are 56% |
| The 20 costliest files, 1,334 CPU s | smith's verbs 27%, compiles 19%, the prover's solver 18%, the tests' programs 18%, processes too short to sample 12%, the runner 3%, links 2% |
| Sanitizers on 548 in-process tests | 2.3 times the CPU |

**The cause that was fixed.** smith's SHA-256 (`runtime/package.c`) ran at about 100 MB/s optimized and 43 MB/s in the dev profile, and smith hashes its own 437 MB binary: twice after every link (the step cache's key for the compiler), and once in every cache folder that meets a build of smith for the first time, which is every test that gives smith a cache of its own. It now uses the processor's SHA instructions where it has them (x86 SHA-NI found with `cpuid`, the ARMv8 ones on Apple's arm64), checked against the portable loop on one block before first use; the portable loop stays for every other processor. `tests/sha256_test.smith` compares the two ways in process, and its test with `macos` in the name is where the ARM instructions run before a merge. The 437 MB hash in 0.39 s optimized and 1.45 s in the dev profile.

| Test file, dev smith, CPU s | Before | After |
|---|---|---|
| `cache_modules_test` | 116.6 | 50.9 |
| `test_cache_test` | 135.8 | 52.6 |
| `lint_changed_test` | 79.4 | 31.7 |
| `build_time_test` | 63.9 | 26.5 |
| `build_isolation_test` | 30.3 | 14.4 |
| A first `smith build` of hello world in an empty cache folder | 13.0 s | 5.15 s |

**Not built, proposed in #3076:** the test headers' step on every plain build (4.4 s of each edit), the coverage test on a small tree, the prover's tests keyed by what they read, corpus cases in process.
