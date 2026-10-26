---
type: design
title: Slots limit heavy jobs on a host
description: smith build, check and test take a slot from a host-wide counter of flock'd files, a waiting CI job goes first, and a smith run by a smith takes none.
tags: [ci, process, machine-load]
---
# Slots limit heavy jobs on a host

Many lanes and two CI runners share one Mac, and its load reached 100 to 170 (#360): CI jobs queued and test limits ran out. So `smith build`, `smith check` and `smith test` (the verbs that start heavy work; `runtime/slots.c`) take a **slot** before they start.

**The counter.** A slot is one of the files `slot-0 ... slot-(n-1)` in a folder of the host (`$SMITH_SLOTS_DIR`, else `/tmp/smith-slots-<uid>`; not `$TMPDIR`, which differs between a shell and a launchd service) with an `flock` held by the process; the file holds "pid verb in folder". There is no daemon and no pid file: the OS drops the lock when the process ends, however it ends (`kill -9` included), so a lock is never stale. The default is cores / 4 slots (at least 1); `SMITH_SLOTS=n` sets it, and `SMITH_SLOTS=0` turns the limit off. A job that waits prints `smith: waiting for a slot (n jobs running: <who>)` to stderr, again every 30 s, and polls every 200 ms. After 4 hours it runs without a slot and says so.

**Nesting.** The holder sets `SMITH_SLOT_HELD=<its pid>` in its environment. A smith that finds that variable names a live process takes no slot, so `smith check` running `smith build` running the test runner, and the tests that start smith, use the one slot of the outermost job (a dead pid is ignored: the variable of a long-gone holder means nothing). The slot is held by the smith process, not by its children (the lock is close-on-exec), so a child that outlives it does not hold the slot.

**CI goes first.** A job with `SMITH_PRIORITY=ci` (the workflow's Full check step) holds a shared lock on `ci-waiting` while it waits. Any other job checks it before each try and takes no slot while it is locked, so the next free slot is the CI job's. The check is not atomic: a local job that tested just before the CI job began waiting may still take that slot, and the CI job gets the next one. Local jobs are not queued among themselves (whichever polls first wins).

**Not covered.** `smith lint`, `fmt` and `prove` take no slot of their own (lint and prove run `smith build` first, which does); the test runner started by hand, outside `smith test`, takes none. `smith --smith-slots PROGRAM` takes a slot and runs the program: it is for `tests/slots_test.smith`.
