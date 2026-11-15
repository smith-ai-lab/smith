---
type: specification
title: Projects and packages
description: How a folder becomes a project by convention, what smith new creates, the standard library, vendored dependencies, build steps and tasks in project.smith, and the command line.
---

# Projects and packages

A project is a folder, and its layout says most of what a build needs to know. A folder that holds one file, `main.smith`, is a complete project: `smith run` builds it and runs it, and `smith test` runs its tests.

- **The root** is the nearest folder, at or above where the command ran, that holds a `project.smith`. When there is none, it is the folder the command ran in.
- **The entry** is `main.smith` at the root. `smith build` and `smith run` can name another file instead, as in `smith run hello.smith`, and that file's `main` is where the program starts.
- **A module's name** is its path from the root, and the standard library lives under `std`.
- **The executable** is named after the entry file without `.smith`, so `hello.smith` gives `hello`. When the entry is `main.smith` it takes the package's name, or the root folder's name when there is no `project.smith`.

## Starting a project

`smith new serving` creates a folder with three files, so that `smith run` and `smith test` work at once.

```
serving/
  project.smith
  main.smith
  main_test.smith
```

`main.smith` prints a greeting from a public function, and `main_test.smith` holds a test of that function, as [Tests](tests.md) describes.

```
pub fn greeting() -> str { "hello world" }

fn main() ! console {
  print(greeting())
}
```

`project.smith` marks the root and describes the package. A new one has nothing to list yet.

```
use std.project (Package)

package = Package(name: "serving")
```

A loose file or folder needs no `project.smith`, which suits a small program written to try something. The file is needed once the project has a dependency, a build step, or a task, all described below. `smith vendor add` creates it when it is missing.

## The standard library

The standard library is large, so that outside code is rarely needed. It covers arrays, differentiation, and neural network layers; tasks, networking, and an HTTP server; serialization through `derive`; files, time, random numbers, text, hashing, and compression; and the platform layer for windows and events. Each module is described in [library/](../library/index.md).

## Dependencies, build steps, and tasks

Outside code is vendored: it is copied into the project as ordinary modules and imported like any other, as in `use vendor.toml.parse (parse)`. There is no registry and there are no version ranges. `project.smith` records where each vendored folder came from and its content hash.

```
use std.project (Generate, Package, Vendored)

package = Package(
  name: "site",
  vendored: [
    Vendored(path: "vendor/toml", source: "https://example.com/toml", hash: "b3:9f2c"),
    Vendored(path: "vendor/sqlite", source: "https://example.com/sqlite", hash: "b3:41ae"),
  ],
  generate: [
    Generate(run: route_table, inputs: "routes/", output: "generated/routes.smith"),
  ],
)

fn route_table(dir: Dir) -> Source ! fs, alloc {
  render_router(files(dir))
}

pub fn deploy(env: str) -> Result[(), DeployError] ! fs, net, alloc {
  binary = try build(package, target: .linux_x64)
  upload(binary, to: env)
}
```

- Every dependency is listed by the project, including dependencies of dependencies. The build checks each folder against its hash and never touches the network.
- A vendored function's effects are in its signature like any other, and a caller must declare them to call it. A parser with no effects is pure, whatever it contains.
- Nothing runs because a dependency is present. Its code runs only when the project calls it.
- A build step is a function listed under `generate`. The build runs it before compiling, on every build, so generated source cannot go stale. A step may read the project's files and nothing else, with no network and no clock. Its output is an ordinary source file, which is how file-based routing, embedded assets, and types generated from a schema file are done.
- A task is a public function in `project.smith`, run by name, as in `smith deploy --env staging`. Its parameters are its command-line flags and its effects say what it may do. Running another program needs the `exec` effect.
- There are no macros that rewrite code in place.

A project that has grown looks like this:

```
serving/
  project.smith
  main.smith
  main_test.smith
  model/
    gpt.smith
    gpt_test.smith
  routes/
    health.smith
  generated/
    routes.smith
  vendor/
    toml/
      parse.smith
```

## The command line

The command line is small, and every public function in `project.smith` is also a command.

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

- `smith build` writes the executable to the folder the command ran in, and replaces a file already there.
- `--target` chooses what to build for, as in `smith build --target macos_arm64`. Its values are the [target names](../compiler.md#backends). Without it, the target is the machine the compiler is running on, and it is an error if that machine is not a target.
- `smith run` runs only what the machine it is on can run, so it rejects any other target.
- A task's parameters are named flags. A `bool` is a bare flag, a parameter with a default or of type `Option` may be left out, and there are no positional arguments.
- A `Result` error prints its message and exits non-zero.
- Built-in command names are reserved, so a task named `build` is a compile error.
- `smith build`, `smith run`, and `smith test` fail only on errors. `smith check` also reports warnings, such as dead code, unused variables, and unused imports, and fails if there are any. What each command compiles, and what the reporting commands print, is described in [compiler.md](../compiler.md#structure).
- `smith vendor add` shows a dependency's own vendored list, and writes entries only for what is approved.

Next: [Tests](tests.md)
