---
type: specification
title: Cloudflare
description: std.cloudflare, with which a program runs as a Cloudflare Worker: durable, which answers store, and worker, which writes what Cloudflare needs beside a built module.
status: planned
---

# Cloudflare

The module `std.cloudflare` holds what a program needs to run as a Cloudflare Worker. The compiler knows nothing of Cloudflare: it builds a [WebAssembly module](../compiler.md#the-host-of-a-webassembly-module) whose imports are the effects its start carries, and this module supplies the rest. A program is written with no line that names it, and only a [deployment](../language/deployments.md) does.

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

After `smith deploy`, the folder `dist` holds the module, the file of JavaScript that Cloudflare starts, and the settings Cloudflare's own tool reads to publish them.

## What answers each effect

| Effect | Answered by |
| --- | --- |
| [`http`](http.md) | The Worker: each request Cloudflare hands it is a request of `http`, and what the program gives is its response. The deployment leaves `http` to it |
| [`store`](store.md) | `durable()`, which keeps each value in the storage of a Durable Object |
| [`messages`](mcp.md) | `http`, through [`at`](http.md#a-server-of-messages-at-a-path), or the request itself for a program that is only a server of messages |

`durable` keeps both promises of [`store`](store.md#what-it-promises): a read sees every write before it, and the writes of one request are kept together or none is. Cloudflare's store of keys whose reads may be behind its writes keeps neither, so nothing here answers `store` with it.

## What worker writes

`worker(module, to: folder)` takes what `build` gave for `.wasm` and writes three things into the folder: the module, a file of JavaScript that receives each request, supplies the module's imports from it, and calls the module, and the file of settings that names them. It is written in Smith, as the handlers are, and it writes only what the module imports: a program that keeps nothing has no Durable Object in its settings.

## The forms

| Written | Is |
| --- | --- |
| `with durable()` | `store` answered by the storage of a Durable Object |
| `worker(module, to: folder)` | The module, its JavaScript, and its settings, written to the folder |
