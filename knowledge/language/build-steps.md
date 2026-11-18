---
type: specification
title: Build steps
description: A function listed under generate in project.smith, which the build runs before compiling to write an ordinary source file.
status: planned
---

# Build steps

A build step is a function listed under `generate` in `project.smith`. The build runs it before compiling, and its output is an ordinary source file.

```
use std.project (Generate, Package)

package = Package(
  name: "site",
  generate: [
    Generate(run: route_table, inputs: "routes/", output: "generated/routes.smith"),
  ],
)

fn route_table(dir: Dir) -> Source ! fs, alloc {
  render_router(files(dir))
}
```

`route_table` reads the folder `routes/` and gives the source of `generated/routes.smith`.

## It runs on every build

The build runs the step before compiling, on every build, so generated source cannot go stale.

## What a step may do

A step may read the project's files and nothing else, with no network and no clock. Its effects say so, so `route_table` lists `fs` and `alloc`. A step that lists `net` or `time` is refused.

```
fn stamped_routes(dir: Dir) -> Source ! fs, time, alloc {
  render_router(files(dir))
}
```

Listed under `generate`, `stamped_routes` is refused, since it asks for `time`.

## What it gives

Its output is an ordinary source file, which is how file-based routing, embedded assets, and types generated from a schema file are done.

```
use generated.routes (routes)
```

`generated/routes.smith` is the module `generated.routes`, so the project imports from it like from any other module.

## No macros

There are no macros that rewrite code in place.

## The forms

| Written | Means |
| --- | --- |
| `Generate(run: f, inputs: dir, output: file)` | Before every build, runs `f` on `dir` and writes its source to `file` |
| `fn f(dir: Dir) -> Source ! fs, alloc` | A step: it reads the project's files and has no network and no clock |

Next: [Project tasks](project-tasks.md)
