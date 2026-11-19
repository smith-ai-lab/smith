---
type: specification
title: How a task ends
description: Three things end a task before its work is done, and what is dropped and closed when one does.
status: planned
---

# How a task ends

Three things end a task before its work is done. Two are failures: running out of memory, and a console write that the system refuses, as when output is piped to a program that has stopped reading. The third is being [cancelled](cancellation.md).

```
use std.console (console, print)

fn log_all(lines: List[str]) ! console, alloc {
  loop line in lines {
    print(line)
  }
}
```

If the system refuses a `print` of `log_all`, the task that runs it ends there, and the lines after it are never printed.

## What happens in each case

In each case the task's arena is dropped whole, the `fs` and `net` handlers close the handles it left open, and the code it was running sees nothing and runs no further. No other task shares its memory, so none is left in a broken state.

```
fn header(path: str) -> Result[List[Byte], FsError] ! fs, alloc {
  with f = open(path)
  read(f, 0..64)
}
```

`open` and `read` are those of [`std.fs`](../library/fs.md). If the task running `header` ends in the middle of `read`, the `fs` handler closes `f`, and `header` does not return.

## Who ends a task

Only the operating system's handlers end a task. A handler written in Smith returns from each operation, as [Async](async.md) says.

## The forms

| What ended the task | Which |
| --- | --- |
| Running out of memory | A failure |
| A console write the system refuses | A failure |
| Being cancelled | Not a failure |

Next: [A failed task](task-failures.md)
