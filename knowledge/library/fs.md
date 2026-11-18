---
type: specification
title: Files
description: The fs effect, for files, a file open for a block, ranges of it read as values, and reading and writing a whole file.
---

# Files

The module `std.fs` declares the `fs` [effect](../language/effect-declarations.md). A program reads a file by asking for a range of its bytes, which it gets as a value.

Most programs read a whole file, or write one.

```
use std.fs (read_all, write_all, FsError)

fn copy(from: str, to: str) -> Result[(), FsError] ! fs, alloc {
  bytes = try read_all(from)
  write_all(to, bytes)
}
```

| Function | Does |
| --- | --- |
| `read_all(path)` | Gives every byte of the file |
| `write_all(path, bytes, executable: false)` | Makes the file, or replaces it, with these bytes. `executable` makes it one the system will run |

Both are written in Smith with the operations below, and a program that needs less than a whole file uses those. `open` is written for [`with`](../language/with.md): it lends the file to the rest of the block and closes it afterwards, on every path.

```
use std.fs (open, read, size, FsError)

fn header(path: str) -> Result[List[u8], FsError] ! fs, alloc {
  with f = open(path)
  read(f, 0..64)
}
```

`read` gives the bytes of the file that lie in the range, which are fewer than the range asks for where the file ends first, and none past its end. A file too large to hold is read a range at a time. Each range is a new value, and one that replaces the last in a `var` uses the same memory.

```
enum FsError {
  missing
  denied
  failed: str
}

effect fs {
  fn open_file(path: str) -> Result[File, FsError]
  fn create_file(path: str, executable: bool = false) -> Result[File, FsError]
  fn close_file(f: take File)
  fn size(f: File) -> Result[Nat, FsError]
  fn read(f: File, range: Range) -> Result[List[u8], FsError] ! alloc
  fn write(f: File, at: Nat, bytes: List[u8]) -> Result[(), FsError]
}
```

| Operation | Does |
| --- | --- |
| `open_file` | Opens a file that exists, to read it |
| `create_file` | Makes a file, or empties one that exists, to write it |
| `close_file` | Closes the file, which cannot be used again |
| `size` | Gives how many bytes the file holds |
| `read` | Gives the bytes in a range of the file |
| `write` | Puts the bytes in the file from the place `at` |

- **A place in a file is a number the program holds.** A file has no position of its own that a read moves, so two tasks read one file without disturbing each other, and reading any part costs the same as reading the next.
- **What is read is a value.** It never changes after it is read. If another program changes the file, a later read gives other bytes, as reading the console twice gives two lines.
- **A `File` is used exactly once,** as [Ownership](../language/ownership.md) says. So a file that is not closed, is closed twice, or is used after it is closed does not compile. `open` and `create` take care of it, and most code writes only the `with` line.
- **How a range is fetched is not the program's business.** The system may read it, read ahead, or map the file into memory and copy from there. The source is the same, and [under tasks](../language/tasks.md) a read suspends its task.
- **Every failure is a value.** No operation ends the program.

A handler supplies the six operations, so a test runs code that reads and writes files over files kept in memory, as `memory_fs` does under [Handlers](../language/handlers.md).
