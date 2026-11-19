---
type: specification
title: Deployments
description: A deployment, a public function of project.smith that installs the handlers that answer a program's effects and calls main, which is run by name on this machine and is where a built executable starts.
status: planned
---

# Deployments

A program asks for what is outside it through [effects](effects.md), and does not say what answers them: a server asks for each request through `http` and keeps what it keeps through `store`. A deployment says what answers them. It is a public function of `project.smith` that installs [handlers](handlers.md) and calls `main`.

```
use main (main)
use std.http (listen)
use std.project (Package)
use std.store (files)

package = Package(name: "notes")

pub fn serve(port: Nat = 8080) ! net, fs, alloc {
  with listen(port: port)
  with files(folder: "data")
  main()
}
```

Here `main` carries `http`, `store`, and `alloc`. `listen` answers `http` from a socket, through `net`, and `files` answers `store` from a folder, through `fs`. So `serve` carries only `net`, `fs`, and `alloc`, which the system provides.

Nothing here is new. A deployment is a [task](project-tasks.md), its settings are its parameters, a handler is installed with [`with`](with.md), and what a handler needs told, as a port, is an argument.

## Running one

A deployment is run by name, as every task is, and its parameters are flags.

```
smith serve --port 9000
```

This runs the program on this machine, answering requests on port 9000.

## Building one

`build` takes the function an executable starts at. With none, it starts at `main`.

```
use main (main)
use std.cloudflare (durable, worker)
use std.project (Package, build)

package = Package(name: "notes")

pub fn cloud() ! http, alloc {
  with durable()
  main()
}

pub fn deploy() -> Result[(), DeployError] ! fs, alloc {
  module = try build(package, start: cloud, target: .wasm)
  try worker(module, to: "dist")
}
```

`smith deploy` builds a WebAssembly module that starts at `cloud`, and writes beside it what its host needs, as [Cloudflare](../library/cloudflare.md) says. `cloud` answers `store` and leaves `http`, so `http` is what the module imports, and the host supplies it.

## Every effect is answered

An executable is built only when everything its start carries has an answer on its target.

- On a native target, the start may carry the effects [the system provides](provided-effects.md) and no other.
- On WebAssembly, each effect the start carries is an import of the module, as [The host of a WebAssembly module](../compiler.md#the-host-of-a-webassembly-module) says.

```
pub fn forgot(port: Nat = 8080) ! net, alloc {
  with listen(port: port)
  main()
}
```

`forgot` is refused, because `main` carries `store`, nothing in `forgot` answers it, and `forgot` does not carry it. A deployment that carried `store` to say so would be refused where it is built for a native target, because no system provides `store`.

## One program, several deployments

A project has as many deployments as it has places to run. The program is the same for each: `serve` and `cloud` above call the same `main`, and no line of `main` says which one runs it. A test is one more: it calls the functions of the program under handlers of its own, as [Handlers](handlers.md) shows.

## The forms

| Written | Is |
| --- | --- |
| A `pub fn` of `project.smith` that installs handlers and calls `main` | A deployment |
| `smith serve --port 9000` | Runs the deployment `serve` on this machine, with `port` as 9000 |
| `build(package, start: f, target: t)` | An executable for `t` that starts at `f` |
| `build(package, target: t)` | An executable for `t` that starts at `main` |
