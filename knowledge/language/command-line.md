---
type: specification
title: The command line
description: The smith commands, how build, run, and check take a file and a target, and what each prints when it fails.
status: partial
built:
  - "`smith build`, `smith run`, and `smith check` on `main.smith` or a named file"
---

# The command line

`smith` takes a command, and the command takes a file, or the entry of the project when none is named. `smith run` builds a program and runs it.

```
fn main() ! console {
  print("hello world")
}
```

With this in `hello.smith`, `smith run hello.smith` prints `hello world`.

## Build

`smith build` compiles a program and writes its executable to the folder the command ran in, and replaces a file already there. It prints nothing when it succeeds.

```
smith build hello.smith
```

This writes `hello` beside the command, as [Projects](projects.md) names it. `smith build` with no file compiles `main.smith`. It writes no executable when the program has an error.

```
fn main() ! console {
  print(1 +)
}
```

Built from `broken.smith`, this prints "broken.smith:2:12: expected an expression, found `)`", the line with a caret under the `)`, and exits with a failure status.

## Run

`smith run` builds the program as `smith build` does, then runs it. A word after the file is passed to the program, as [main](main.md) describes.

```
smith run hello.smith
```

## Check

`smith check` checks a program and writes no code. It reports the errors, and when there are none it reports warnings, such as dead code, unused variables, and unused imports, and it fails if there are any.

```
fn unused(x: i32) -> i32 { 1 }

fn main() ! console {
  y = 3
  print("hi")
}
```

`smith check` on this prints two warnings and fails.

```
w.smith:1:4: the function `unused` is dead code: `main` does not reach it
  1 | fn unused(x: i32) -> i32 { 1 }
    |    ^
  fix: delete it, or use it from code that `main` reaches
w.smith:4:3: `y` is defined in `main` and never read
  4 |   y = 3
    |   ^
  fix: delete the binding, or read `y`
```

`smith build` and `smith run` fail only on errors, so both build this program. What each command compiles, and what the reporting commands print, is described in [compiler.md](../compiler.md#structure).

## The target

`--target` chooses what to build for, as in `smith build --target macos_arm64 hello.smith`. Its values are the [target names](../compiler.md#backends). Without it, the target is the machine the compiler is running on, and it is an error if that machine is not a target: "this machine, ..., is not a target; choose one with --target".

A name that is not a target is refused with the names that are.

```
smith build --target nope hello.smith
```

This prints "unknown target `nope`; the targets are linux_x64, macos_arm64, linux_arm64, macos_x64, windows_x64, windows_arm64".

`smith run` runs only what the machine it is on can run, so it rejects any other target.

```
smith run --target macos_arm64 hello.smith
```

On a machine that is not a `macos_arm64` machine this prints "smith run runs only programs for this machine, and macos_arm64 is not its target".

## Timings

`--timings` prints how long each stage of the build took.

```
smith build --timings hello.smith
```

This prints a line for each stage, `lex`, `parse`, `check`, `inline`, `fold`, `common`, `remove`, `emit`, and `write`, with its time, and a `total`.

## The other commands

The other commands each have a page of their own, or a section of one.

```
smith new serving            create a project
smith check                  check everything and report warnings, no code generated
smith build                  compile main.smith, or the file named
smith run                    build it and run it
smith test                   run the tests
smith fmt                    format every file
smith vendor add <url>       fetch, hash, and add an entry
smith vendor update <path>   re-fetch one folder and update its hash
smith hole                   what a hole needs
smith find                   definitions matching a signature
smith deps                   what a definition uses, and what uses it
smith cost                   allocation and operation counts

smith deploy --env staging   a task from project.smith
```

[Projects](projects.md) describes `smith new`, [Dependencies](dependencies.md) describes `smith vendor`, [Project tasks](project-tasks.md) describes a task from `project.smith`, and [Running tests](running-tests.md) describes `smith test`. `smith hole` is described under [hole](hole.md).

## The forms

| Written | Means |
| --- | --- |
| `smith build [file]` | Compiles `main.smith`, or the file, and writes the executable |
| `smith run [file] [word...]` | Builds, then runs, with the words passed to the program |
| `smith check [file]` | Reports errors, or else warnings, and writes no code |
| `--target <name>` | Builds for that target, and `smith run` rejects one the machine cannot run |
| `--timings` | Prints how long each stage took |
| `smith new`, `smith vendor`, `smith test`, a task | Described on the pages above |

Next: [The standard library](standard-library.md)
