---
type: architecture
title: std::files, the direct-call files module
description: Files and folders as direct calls returning result(T, files::Error), over the files handler of the thread, with an in-memory fake and memory from the current allocator.
tags: [files, handlers, result, mem]
---
# std::files

`lib/std/files.smith` (skill smith-files) is the direct-call files module (issue #240; the module design is #450).

- **Through the handler.** `open`, `read`, `write` and `close` reach the thread's `SmithFileHandler` as libc's functions do (the handlers runtime interposes them); `stat`, `mkdir`, `remove`, `rename`, `list`, `map`, `unmap`, `getcwd`, `realpath`, `access`, `readlink`, `symlink`, `lseek`, `pwrite`, `ftruncate`, `fchmod` and `flock` are members the module calls itself (`include/smith/handlers.h`, owner 2026-10-26: appended to the struct, a null member is libc's own call, so every existing handler keeps working). The files fake (`files::fake()`) fills all of them over in-memory files and folders; a test sets it with `files::set_handler` and `defer`.
- **Errors.** What the world answers (ENOENT, EEXIST, EACCES, ENOSPC of a real disk...) is a `files::Error` value. A bug is a checked failure (the panic-line audit of #240): a mode a call does not take (EINVAL), a descriptor that is not open (EBADF, so a silent double close), and the fake's fixture limits.
- **Memory.** `read_file`, `list` and the fake's bytes come from `mem::alloc` and belong to the caller's current region; nothing is freed one object at a time (#439). A test installs an arena and checks the heap saw nothing.
- **Reads log.** With `SMITH_READS_LOG` set (read through `std::env`), `open` for `Read` (so `read_file`), `map` and `list` append the real path they open to that file, a folder's ending in `/`(#739, [coverage-selection](coverage-selection.md)), so a test that reads through `files` is selected by `smith test --changed` when the file changes; nothing is written while a files handler (the fake) is set.
- **Names.** The C functions are `smith_fs_*`; the types are `SmithFiles*`.
- **Limits today.** `try` across results with different success types warns (`-Wenum-compare`), so the module tests with `==` and `switch`; a result holds a slice through a typedef (`files::Bytes`); `Entry.name` is a mutable `char` slice with a NUL after it.
