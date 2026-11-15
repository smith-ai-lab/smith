---
type: specification
title: Projects and packages
description: The standard library, vendored dependencies, project.smith, build steps, tasks, and the command line.
---

# Projects and packages

The standard library is large, so that outside code is rarely needed. It covers arrays, differentiation, and neural network layers; tasks, networking, and an HTTP server; serialization through `derive`; files, time, random numbers, text, hashing, and compression; and the platform layer for windows and events.

Outside code is vendored: it is copied into the project as ordinary modules and imported like any other, as in `use vendor.toml.parse (parse)`. There is no registry and there are no version ranges. One file at the project root, `project.smith`, records where each vendored folder came from and its content hash.

```
use std.project (Generate, Package, Vendored)

package = Package(
  name: "site",
  entry: "main",
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

A module's name is its path from the project root, and the standard library lives under `std`. Every field of `Package` is always written, since there are no default field values.

```
serving/
  project.smith
  main.smith
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

The command line is small, and every public function in `project.smith` is also a command.

```
smith new serving            create a project
smith check                  check everything and report warnings, no code generated
smith build                  compile the entry module
smith run                    build and run it
smith test                   run the tests
smith fmt                    format every file
smith vendor add <url>       fetch, hash, and add an entry
smith vendor update <path>   re-fetch one folder and update its hash

smith deploy --env staging   a task from project.smith
```

- A task's parameters are named flags. A `bool` is a bare flag, an `Option` is optional, and there are no positional arguments.
- A `Result` error prints its message and exits non-zero.
- Built-in command names are reserved, so a task named `build` is a compile error.
- `smith build`, `smith run`, and `smith test` fail only on errors. `smith check` also reports warnings, such as dead code, unused variables, and unused imports, and fails if there are any. What each command compiles is described in [compiler.md](../compiler.md#structure).
- `smith vendor add` shows a dependency's own vendored list, and writes entries only for what is approved.

Next: [Tests](tests.md)
