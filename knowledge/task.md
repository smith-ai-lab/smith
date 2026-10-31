---
type: architecture
title: smith::task, fibers and groups on one thread
description: Tasks on 256 KB mmap stacks with a guard page, switched by assembly, waited for in a task::Group, with the inline scheduler hard-wired (issue #241, first step).
tags: [task, fiber, scheduler]
---
# smith::task

`lib/smith/task.smith` is the first step of issue #241: smith manages lifetimes as scopes, an arena for memory, a group for tasks. Decided there: `main` is always the first task, `wait` and `pause` work in `main`, there is no `task::run`. This page is what exists; the plan (the typed `spawn`, handlers per task, workers on more threads, parking I/O, a simulation scheduler) stays in the issue.

- **A task is a fiber.** One `mmap` holds a guard page (`PROT_NONE`, one page), 256 KB of stack, and at its top the task's own record, so no allocator is involved and a refusal by the system is a checked failure. The mapping is released when the task has ended. Each task is two mappings, so a process holds about 32,000 live tasks under Linux's default `vm.max_map_count` of 65530; a later ticket decides how to go past it.
- **The switch is assembly in the module**: file-scope `__asm__` under `if constexpr` on `target::arch` and `target::os` (arm64 macOS, arm64 Linux, x86-64 Linux; any other target is a `static_assert`). It saves the callee-saved registers on the old stack, stores the stack pointer and loads the new one; a new task's stack holds a frame that returns into a boot stub, which calls the entry with the task. Under AddressSanitizer each switch calls `__sanitizer_start_switch_fiber` and `finish`, and an ended task's mapping is unpoisoned before it is unmapped, so the next stack at that address starts clean.
- **The scheduler is the inline one.** A spawned task runs at once until it ends or pauses, then control returns to the spawner; a task that paused is queued (one FIFO per thread, by whoever resumed it), and `wait` and a `pause` of `main` run the queue in turn. It is deterministic and starts no thread. Making it a handler comes in a later ticket.
- **Exports are `Group`, `wait`, `pause` and `spawn`.** `pause` is the cooperative yield (not called `yield`, which is #436's generator statement). `spawn(&group, f, &arg)` is a generic over `T`: `f` is `void (*)(T *)` and `arg` a `T *`, checked by a `static_assert`, so no `void *` reaches a user; it wraps a private `spawn_raw`. It stands until the lambda form of #945, which replaces it (the old form is deleted, not kept).
- **Not here:** handlers and `errno` per task, workers on more threads, parking I/O, cancellation, and more than about 32,000 live tasks on Linux (two mappings each against `vm.max_map_count`).

Tests: `lib/smith/task_test.smith`.
