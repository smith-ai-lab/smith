---
type: specification
title: The standard library
description: What the standard library covers, which is why outside code is rarely needed, and how its modules are imported from std.
status: planned
---

# The standard library

The standard library is large, so that outside code is rarely needed. Its modules live under `std`, and a program imports from them like from any other module.

```
use std.test (Failed, expect)
use std.project (Package)
```

## What it covers

- arrays, differentiation, and neural network layers;
- tasks, networking, and an HTTP server;
- serialization through `derive`;
- files, time, random numbers, text, hashing, and compression;
- the platform layer for windows and events.

Its modules are described in [library/](../library/index.md), one page for each. A module that declares an effect lists the effect's operations there.

## What needs no import

A name of the [prelude](prelude.md) is in scope with no `use`. The prelude holds only what the syntax means, so everything else is imported, `print` and `console` among them, which are in [std.console](../library/console.md).

Next: [Dependencies](dependencies.md)
