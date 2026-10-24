---
name: smith-io-files
description: "Files in smith::io: write modes (io::Replace, io::Atomic), folders, maps, the environment (io::args_env) and file info. Load when reading, writing, listing or mapping files, or reading the environment, with io::files_* or io::args_*."
---

# Files, folders, maps and the environment

## Write modes, folders, maps, the environment

These block and run at once (they do not queue behind pending asynchronous file operations), on the real io and the fake alike:

| Call | Does |
| --- | --- |
| `io::files_info(files, path)` | `folder`, `writable`, `size`; `ENOENT` if nothing is there |
| `io::files_list(files, arena, folder)` | entries (`name`, `folder`, `size`) sorted by name, without `.` and `..` |
| `io::files_make_folder(files, path)` | like `mkdir -p`; a folder that is there is success |
| `io::files_write(files, path, data, mode)` (`data` is a `const u8[:]`; a caller with other data writes a byte view, `((const u8 *)xs)[0:n * sizeof(f32)]`) | writes the bytes as the mode says: `io::Replace` (create or replace), `io::Atomic` (write `<path>.tmp` and rename it over the path, so nobody sees half a file), `io::Append` (add to the end, creating the file) or `io::New` (create only if there is none: `EEXIST` otherwise, which is how a program makes a unique name) |
| `io::files_remove(files, path)` | deletes a file (a folder is `EPERM`) |
| `io::files_map(files, path)` | maps the file: nothing is read until the memory is touched, so a 150 MB asset costs only the pages used. Private and writable (writes never reach the file), for the life of the program; an empty file is `EINVAL`. The fake gives a copy |
| `io::args_env(&io.args, "NAME")` | the variable's value, or `nullptr`; the real io asks the process each time |
| `io::processes_cores(&io.processes)` | how many processes can run at once |

For several arenas with different lifetimes, such as one reset every frame, carve them out of `io.arena` with `smith_arena_carve` (see smith-arena): the 64 GiB reservation commits pages only as they are used.

Reading a large file with `io::files_read` copies it into the arena; mapping it does not. Choose the map for assets read in place, the read for text.

A program that runs several copies of itself spawns `args.values[0]` with `io::processes_spawn`, waits with `io::processes_wait_async` on all of them in one loop, and gets their results from files they wrote with `io::files_write` and `io::Atomic`, in a folder named from `io.key` under `$TMPDIR`; each copy builds what it needs again, since nothing is shared.

Also in this family: [smith-io](../smith-io/SKILL.md), [smith-io-capabilities](../smith-io-capabilities/SKILL.md), [smith-effects](../smith-effects/SKILL.md), [smith-io-errors](../smith-io-errors/SKILL.md), [smith-io-processes](../smith-io-processes/SKILL.md), [smith-io-async](../smith-io-async/SKILL.md), [smith-io-net](../smith-io-net/SKILL.md), [smith-io-fake](../smith-io-fake/SKILL.md), [smith-io-c](../smith-io-c/SKILL.md).
