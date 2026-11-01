---
name: smith-files
description: "Using std::files (import std::files; files::read_file, write_file, open, read, write, close, info, list, make_folder, remove, move, map, files::fake): files and folders by direct calls that return a result, with an in-memory fake for tests. Load when code imports std::files or includes <smith/files.h>, or on reading or writing a file."
---

# Files by direct calls: std::files
Call `files::` anywhere. A call returns `result(T, files::Error)`; the error is what the world said (`NotFound`, `Exists`, `Denied`, `IsFolder`, `NotFolder`, `NotEmpty`, `NameTooLong`, `NoSpace`, `ReadOnly`, `TooManyOpen`, `Other`). Paths are `const char path[:]` (a string literal converts). Memory a call returns comes from the current allocator (smith-mem) and is the caller's region: install an arena first and never free it one block at a time.

```smith
module scratch;
import std::arena;
import std::files;
import std::mem;
export usize bytes_in(const char path[:], arena::Arena *a) {
    arena::ArenaAllocator allocator = arena::allocator(a);
    mem::Allocator *before = mem::set_allocator(&allocator.base);
    defer mem::set_allocator(before);
    files::BytesResult text = files::read_file(path);
    switch (text) {
    case .ok: return countof(text.ok);
    case .error: return 0;
    }
}
```
| Call | Does |
| --- | --- |
| `read_file(path)` / `write_file(path, data, mode)` | the whole file as `u8[:]`; write returns the bytes written. Modes: `Replace`, `Append`, `New` (`Exists` if taken), `Atomic` (`<path>.tmp` then rename) |
| `open(path, mode)` `read(file, buffer)` `write(file, data)` `close(file)` | by pieces; `mode` is `Read`, `Replace`, `Append` or `New`; read fills the buffer and returns the count (0 is the end); close returns an `option(Error)` |
| `info(path)` / `list(folder)` | `Info{folder, writable, size}`; entries sorted by name (name, folder, size), without `.` and `..` |
| `make_folder(path)` / `remove(path)` / `move(from, to)` | `mkdir -p`; delete a file (a folder is `Denied`); rename |
| `map(path)` / `unmap(bytes)` | a private writable mapping (the fake gives a view); an empty file is an empty slice |

- **Misuse is a checked failure**: a mode a call does not take (`open` with `Atomic`), a descriptor not open (a second `close`), the fake's limits.
- A result is read where it is tested (`switch (r)`, smith-result); a call's result goes in a variable first.

**The fake, for tests:**
```smith
module scratch_fake;
import std::arena;
import std::files;
import std::mem;
export usize config_size(arena::Arena *a) {
    arena::ArenaAllocator allocator = arena::allocator(a);   // the fake's bytes come from the allocator in force
    mem::Allocator *outside = mem::set_allocator(&allocator.base);
    defer mem::set_allocator(outside);
    files::Fake fake = files::fake();                        // in-memory files and folders: 64 entries, 16 open
    files::Handler *before = files::set_handler(&fake.base); // the thread's files handler (include/smith/handlers.h)
    defer files::set_handler(before);
    files::fake_add(&fake, "config.txt", ((const u8 *)"key=1")[0:5]); // a file to read; code under test runs unchanged
    files::BytesResult text = files::read_file("config.txt");
    switch (text) {
    case .ok: return countof(text.ok);
    case .error: return 0;
    }
}
```
The fake's bytes come from the allocator in force when written (a test runs under `mem::checked`). With `SMITH_READS_LOG` set (the test runner sets it), `open` for reading, `read_file`, `map` and `list` append the real path to that log, so `smith test --changed` selects the test when the file changes; a fake handler writes nothing. While set it also answers C's `open`, `read`, `write`, `close` and `fopen`.

C names are `smith_fs_*`. See also [smith-mem](../smith-mem/SKILL.md), [files](../../../knowledge/files.md).
