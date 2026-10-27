---
type: architecture
title: smith::task, fibers and groups on one worker
description: Tasks on 256 KB mmap stacks with a guard page, switched by assembly, waited for in a task::Group, with a scheduler that is a handler of the thread and an inline scheduler first; each task has its own errno and handler tables (issue #241, first step).
tags: [task, fiber, scheduler, handlers]
---
# smith::task

`lib/smith/task.smith` (skill smith-task) is the first step of issue #241: the owner's principle is that smith manages lifetimes as scopes, an arena for memory, a group for tasks, `set_allocator` for which handler is installed. Decided there: `main` is always the first task, `spawn` and `wait` work in `main`, there is no `task::run`. This page is what exists; the plan (more workers, parking I/O, `pin_main`, a simulation scheduler) stays in the issue.

- **A task is a fiber.** One `mmap` holds a guard page (`PROT_NONE`, one page), 256 KB of stack, and at its top the task's own record, so no allocator is involved and a refusal by the system is a checked failure. `resume(task)` enters it and returns when it yields or finishes; a finished task's mapping is released by that `resume`.
- **The switch is assembly in the module** (file-scope `__asm__` under `if constexpr` on `target::arch` and `target::os`: arm64 macOS and Linux, x86-64 Linux; the x86-64 one follows the System V ABI and has not been run). It saves the callee-saved registers on the old stack, stores the stack pointer and loads the new one; a new task's stack holds a frame that returns into a boot stub, which calls the entry with the task. Under AddressSanitizer each switch calls `__sanitizer_start_switch_fiber` and `finish`; the first task's bounds are learned the first time it is left.
- **The scheduler is a handler** (`task::Scheduler`: `spawn`, `yield`, `wait`; `set_scheduler` returns the previous), held per thread in the module and saved and restored with the task, so a task inherits its spawner's. None set is the inline scheduler: `spawn` runs the task at once until it finishes or yields, yielded tasks wait in one FIFO per thread, and `wait` and `main`'s `yield` run them in turn. It is deterministic and starts no thread, the base for the worker and simulation schedulers.
- **Per-task handler tables.** `smith_handlers_tables_save` and `_load` (`lib/handlers.c`) read and set all eight thread tables through the setters (the active counters stay right); a task switch saves the leaver's and loads the entered task's, plus `errno`. A new task starts with a copy of its spawner's tables. This is the table swap the ticket named the per-task tables pointer; a pointer swap is a later optimization.
- **The job is an embedded struct, not a callback with `void *`.** `spawn(&group, &job.base)` takes a `task::Job` (`run(self)`) that is the first member of the user's struct, as an allocator is. The entry of a typed `spawn(&group, f, args...)` needs a function per argument list, which the language cannot yet write (a generic has no address, no nested function, `rest` cannot be passed whole: #316 and #400); the typed form will build a job.
- **Not here:** workers on more threads, the first-thread reservation for the platform's event loop, parking I/O, cancellation, and the safety rules over it (#436).

Tests: `lib/smith/task_test.smith`.
