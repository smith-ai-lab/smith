---
type: specification
title: Projects
description: A folder as a project, its root and entry, the names of its modules and its executable, project.smith, and what smith new creates.
status: partial
built: "the entry, `main.smith` or a named file, and the name of the executable"
---

# Projects

A project is a folder, and its layout says most of what a build needs to know. A folder that holds one file, `main.smith`, is a complete project.

```
hello/
  main.smith
```

```
fn main() ! console {
  print("hello world")
}
```

Run in `hello/`, `smith run` builds the project and runs it, and prints `hello world`. `smith test` runs its tests, as [Running tests](running-tests.md) describes.

## The root

The root is the nearest folder, at or above where the command ran, that holds a `project.smith`. When there is none, it is the folder the command ran in.

```
serving/
  project.smith
  model/
    gpt.smith
```

Run in `serving/model/`, a command finds `serving/project.smith` above it, so the root is `serving/`. In `hello/` above, no folder holds a `project.smith`, so the root is `hello/`.

## The entry

The entry is `main.smith` at the root, and its `main` is where the program starts. `smith build` and `smith run` can name another file instead, and that file's `main` is where the program starts.

```
fn main() ! console {
  print("hello")
}
```

With this in `hello.smith`, `smith run hello.smith` prints `hello`. In a folder with no `project.smith` and no `main.smith`, `smith run` with no file is refused: "main.smith: No such file or directory (os error 2)".

## The names a project gives

A module's name is its path from the root, as [Modules](modules.md) describes, and the standard library lives under `std`, as [The standard library](standard-library.md) describes.

The executable is named after the entry file without `.smith`, so `hello.smith` gives `hello`. When the entry is `main.smith` it takes the package's name, or the root folder's name when there is no `project.smith`. An executable for Windows ends in `.exe`, so there `hello.smith` gives `hello.exe`.

| Entry | Root | Executable |
| --- | --- | --- |
| `hello.smith` | any | `hello` |
| `hello.smith`, for Windows | any | `hello.exe` |
| `main.smith` | `hello/`, no `project.smith` | `hello` |
| `main.smith` | `serving/`, whose `project.smith` names the package `serving` | `serving` |

## project.smith

`project.smith` marks the root and describes the package. A new one has nothing to list yet.

```
use std.project (Package)

package = Package(name: "serving")
```

A loose file or folder needs no `project.smith`, which suits a small program written to try something, like `hello.smith` above. The file is needed once the project has a dependency, a build step, or a task, as [Dependencies](dependencies.md), [Build steps](build-steps.md), and [Project tasks](project-tasks.md) describe. `smith vendor add` creates it when it is missing.

## Starting a project

`smith new serving` creates a folder with three files, so that `smith run` and `smith test` work at once.

```
serving/
  project.smith
  main.smith
  main_test.smith
```

`main.smith` prints a greeting from a public function.

```
pub fn greeting() -> str { "hello world" }

fn main() ! console {
  print(greeting())
}
```

`main_test.smith` holds a test of that function, as [Tests](tests.md) describes. `main.smith` is the module `main`, named by its path as every module is, and the test file imports from it as any file would.

```
use main (greeting)
use std.test (Failed, expect)

fn test_greeting() -> Result[(), Failed] {
  expect { greeting() == "hello world" }
}
```

Only what `main.smith` marks `pub` can be listed, so `greeting` is `pub`.

`project.smith` is the one above.

## The forms

| Written | Means |
| --- | --- |
| A folder with `main.smith` | A complete project |
| The nearest `project.smith` above | The root, or the folder the command ran in when there is none |
| `smith run` | Builds and runs the `main` of `main.smith` at the root |
| `smith run hello.smith` | Builds and runs the `main` of that file |
| `project.smith` | Marks the root and describes the package, and is needed for a dependency, a build step, or a task |
| `smith new serving` | Creates `project.smith`, `main.smith`, and `main_test.smith` in `serving/` |

Next: [The command line](command-line.md)
