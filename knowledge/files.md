---
type: architecture
title: smith::files, the direct-call files module
description: Files and folders as direct calls returning result(T, files::Error), over the files handler of the thread, with an in-memory fake and memory from the current allocator; the first replacement of io::Io's files (issue #240).
tags: [files, handlers, result, mem]
---
# smith::files

`lib/smith/files.smith` (skill smith-files) is `io::Io`'s files part without the `Io` value (issue #240, phase 1; the module design is #450). It is additive: `io` is unchanged until the migration phases.

- **Through the handler.** `open`, `read`, `write` and `close` reach the thread's `SmithFileHandler` as libc's functions do (the handlers runtime interposes them); `stat`, `mkdir`, `remove`, `rename`, `list`, `map` and `unmap` are members the module calls itself (`include/smith/handlers.h`, owner 2026-10-26: appended to the struct, a null member is libc's own call, so every existing handler keeps working). The files fake (`files::fake()`) fills all of them over in-memory files and folders and replaces io's fake files; a test sets it with `files::set_handler` and `defer`.
- **Errors.** What the world answers (ENOENT, EEXIST, EACCES, ENOSPC of a real disk...) is a `files::Error` value. A bug is a checked failure (the panic-line audit of #240): a mode a call does not take (EINVAL), a descriptor that is not open (EBADF, so a silent double close), and the fake's fixture limits.
- **Memory.** `read_file`, `list` and the fake's bytes come from `mem::alloc` and belong to the caller's current region; nothing is freed one object at a time (#439). A test installs an arena and checks the heap saw nothing.
- **Names.** The C functions are `smith_fs_*` while io owns `smith_files_*`; the types are `SmithFiles*`. The rename to `smith_files_*` belongs to the deletion of io.
- **Limits today.** `try` across results with different success types warns (`-Wenum-compare`), so the module tests with `==` and `switch`; a result holds a slice through a typedef (`files::Bytes`); `Entry.name` is a mutable `char` slice with a NUL after it.
