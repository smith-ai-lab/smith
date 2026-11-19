---
type: specification
title: The command line
description: The smith commands, how build, run, and check take a file and a target, and what each prints when it fails.
status: partial
built:
  - "`smith build`, `smith run`, and `smith check` on `main.smith` or a named file"
  - "`smith build --target wasm`, which writes a module and the file of JavaScript that loads it"
---

# The command line

`smith` takes a command, and the command takes a file, or the entry of the project when none is named. `smith run` builds a program and runs it.

```
use std.console (console, print)

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
use std.console (console, print)

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
use std.bits (i32)
use std.console (console, print)

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

This prints "unknown target `nope`; the targets are linux_x64, macos_arm64, linux_arm64, macos_x64, windows_x64, windows_arm64, wasm".

`smith build --target wasm hello.smith` writes two files, `hello.wasm`, a WebAssembly module, and `hello.js`, which loads it under `node` and supplies what the module imports, so that `node hello.js` prints what `smith run hello.smith` prints and ends with the same status. [The host of a WebAssembly module](../compiler.md#the-host-of-a-webassembly-module) says more.

`smith run` runs only what the machine it is on can run, so it rejects any other target, `wasm` among them.

```
smith run --target macos_arm64 hello.smith
```

On a machine that is not a `macos_arm64` machine this prints "smith run runs only programs for this machine, and macos_arm64 is not its target".

`smith check` and `smith test` take no `--target`. A program is checked the same for every target, as no type and no rule depends on the machine, so one `smith check` answers for all of them. And a test is run, which only the machine it is on can do, so a test of another target is `smith test` on that machine.

```
smith check --target macos_arm64 hello.smith
```

This prints "unknown flag `--target`" and then how the commands are used.

## Timings

`--timings` prints how long each stage of the build took.

```
smith build --timings hello.smith
```

This prints a line for each stage, `lex`, `parse`, `check`, `inline`, `trim`, `thread`, `fold`, `common`, `remove`, `emit`, and `write`, with its time, and a `total`.

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
| `--target <name>` | On `build` and `run` only: builds for that target, and `smith run` rejects one the machine cannot run |
| `--timings` | Prints how long each stage took |
| `smith new`, `smith vendor`, `smith test`, a task | Described on the pages above |

Next: [The standard library](standard-library.md)
