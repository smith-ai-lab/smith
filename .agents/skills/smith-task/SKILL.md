---
name: smith-task
description: "Using smith::task (import smith::task; task::Group, Job, spawn, wait, yield, set_scheduler): tasks on stacks of their own, waited for in a group with defer, a scheduler that is a handler of the thread, and a task's own errno and handlers. Load when code imports smith::task or includes <smith/task.h>, or on fibers, tasks, wait groups, yielding or writing a scheduler for a test."
---

# Tasks and groups: smith::task

This skill ships in the smith plugin, from `.agents/skills/smith-task/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. It describes what exists today: every task runs on the thread that spawned it. Workers on more threads, waiting on sockets and a typed `spawn(&group, f, args...)` are planned in issue #241 and are not here.

A group is a scope for tasks as an arena is for memory: open it, `defer` its wait. `main` is the first task; there is no `task::run`.

```smith
module crew;

import smith::task;

typedef struct {
    task::Job base; // first: &work.base is what spawn takes
    u64 *total;
    u64 amount;
} Work;

void run(task::Job *self) {
    Work *work = (Work *)self; // the job's own struct, as a handler casts its self
    *work->total += work->amount;
    task::yield();             // let the others run; back here when the scheduler says
    *work->total += work->amount;
}

export u64 sum() {
    u64 total = 0;
    Work a = {.base = {.run = run}, .total = &total, .amount = 1};
    Work b = {.base = {.run = run}, .total = &total, .amount = 10};
    task::Group group = {};
    defer task::wait(&group); // returns when both have finished
    task::spawn(&group, &a.base);
    task::spawn(&group, &b.base);
    return total;             // 11: each ran up to its yield; the deferred wait runs the rest
}
```

- **`spawn(&group, &job.base)`** starts a task that calls `job->run(job)` on its own stack (256 KB of mapped memory below a guard page, so an overflow faults). The job and the group must outlive `wait`. **`wait(&group)`** returns when the group has no task left; tasks waiting on nothing are a checked failure naming the count. **`yield()`** offers the worker; from `main` it runs each parked task once.
- **The inline scheduler** (none set) runs a spawned task at once until it finishes or yields, then returns to the spawner; `wait` and `main`'s `yield` resume the parked tasks in turn, so the order is deterministic.
- **The scheduler is a handler**: `task::set_scheduler(&mine.base)` installs a `task::Scheduler` (`spawn`, `yield`, `wait` members; null is the inline one) and returns the previous, restored with `defer`. A scheduler runs a task with `task::resume(task)` (true when it finished, the task is then freed) and a yielding task goes back with `task::suspend()`. A task inherits its spawner's scheduler.
- **A task keeps its own `errno` and handler tables** (allocator, files, console, env, clock, entropy, net, process, scheduler): it starts with its spawner's, what it sets stays its own across yields, `defer` restores as usual.
- **Under ASan** each switch is announced; the assembly switch is arm64 (macOS, Linux) and x86-64 (Linux, written from the ABI and not yet run).
- **From C**: `<smith/task.h>`: `smith_task_spawn`, `smith_task_wait`, `smith_task_yield`, `smith_task_set_scheduler`. See `lib/smith/task_test.smith`, [knowledge/task.md](../../../knowledge/task.md).
