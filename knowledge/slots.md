---
type: design
title: Slots limit heavy jobs on a host
description: smith build, check and test take a slot from a host-wide counter of flock'd files, a waiting CI job goes first, and a smith run by a smith takes none.
tags: [ci, process, machine-load]
---
# Slots limit heavy jobs on a host

Many lanes and two CI runners share one Mac, and its load reached 100 to 170 (#360): CI jobs queued and test limits ran out. So `smith build`, `smith check` and `smith test` (the verbs that start heavy work) take a **slot** when the executor starts its first real step (a compile, a link, a test; `runtime/slots.c`). A build that is up to date, or whose steps are only functions of build.c (`friction`, `ci-times`), never waits. The slot is taken by the smith that runs the executor and held until it ends; `smith check` and `smith test` run `smith build` children, which take theirs.

**The counter.** A slot is one of the files `slot-0 ... slot-(n-1)` in a folder of the host (`$SMITH_SLOTS_DIR`, else `/tmp/smith-slots-<uid>`; not `$TMPDIR`, which differs between a shell and a launchd service) with an `flock` held by the process; the file holds "pid parent start" and then "pid verb in folder". There is no daemon and no pid file: the OS drops the lock when the process ends, however it ends (`kill -9` included), so a lock of a dead process is never stale (the case of a lock that outlives its process is below). The default is cores / 4 slots (at least 1); `SMITH_SLOTS=n` sets it, and `SMITH_SLOTS=0` turns the limit off. A job that waits prints `smith: waiting for a slot (n jobs running: <who>)` to stderr, again every 30 s, and polls every 200 ms. After 4 hours it runs without a slot and says so.

**Workers (#506).** A slot limits how hard its holder runs, not only how many run: the holder takes the host's cores / slots (at least 1) and sets `SMITH_CORES=<share>` for everything it starts. smith reads `SMITH_CORES` in place of the machine's core count: the executor never runs more jobs than it (checked before each job starts), the test runner's and `io::processes_cores`'s default worker count, `smith lint`'s parallelism. A test sets `SMITH_CORES` as a fake host.

**CI lowers the others.** A CI holder keeps a shared lock on `ci-held` while it lives. A local holder with a slot looks at it (at most once a second) before each job its executor starts and runs one job while it is held, and a local job that takes its slot while a CI job holds one gets `SMITH_CORES=1`. The limits: the share is fixed when the holder starts for the processes it spawns (a test runner already running keeps its workers when CI appears; only the executor's own jobs follow), and CI itself keeps cores / slots, not the whole machine.

**A dead or orphaned holder loses its slot.** The first line of a slot file is "pid parent-pid start-time". A job that finds a slot locked reads it: when that process no longer exists (the lock outlived it in a child), its start time differs (a recycled pid), or its parent is now pid 1 or another than recorded (an orphan: `smith build wait` left behind by a test held slot 1 for 10 hours), the job unlinks the file and takes a fresh one; the old holder keeps a lock on a file nobody opens, and is not killed. A holder started directly by launchd (parent 1) is read as an orphan too.

**Tests.** A smith that a test starts (`scratch::`) counts its slots in a folder of the test's own (`SMITH_SLOTS_DIR` beside its project, set by `tests/scratch.smith`), never the host's, so a test whose jobs must run at once is not held up by CI and the lanes holding the host's slots; `SMITH_SLOT_HELD` still stops a smith the runner started from taking one.

**Nesting.** The holder sets `SMITH_SLOT_HELD=<its pid>` in its environment. A smith that finds that variable names a live process takes no slot, so `smith check` running `smith build` running the test runner, and the tests that start smith, use the one slot of the outermost job (a dead pid is ignored: the variable of a long-gone holder means nothing). The slot is held by the smith process, not by its children (the lock is close-on-exec), so a child that outlives it does not hold the slot.

**CI goes first.** A job with `SMITH_PRIORITY=ci` (the workflow's Full check step) holds a shared lock on `ci-waiting` while it waits. Any other job checks it before each try and takes no slot while it is locked, so the next free slot is the CI job's. The check is not atomic: a local job that tested just before the CI job began waiting may still take that slot, and the CI job gets the next one. Local jobs are not queued among themselves (whichever polls first wins).

**Not covered.** `smith lint`, `fmt` and `prove` take no slot of their own (lint and prove run `smith build` first, which does); the test runner started by hand, outside `smith test`, takes none. `smith --smith-slots PROGRAM` takes a slot and runs the program: it is for `tests/slots_test.smith`.
