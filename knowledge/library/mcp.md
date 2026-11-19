---
type: specification
title: MCP
description: std.mcp, with which a program is a Model Context Protocol server whose tools are plain functions, each described by its own comment and the types it takes and gives.
status: planned
---

# MCP

`std.mcp` makes a program a server of the Model Context Protocol. A tool is a plain function. Its name is the function's name, its description is the comment on the function, and the schema of its input is made from the type it takes.

```
use std.mcp (serve, tool, stdio, Info)

struct DivideInput {
  // The number that is divided.
  a: i64
  // The number it is divided by.
  b: i64
}

// Divides one whole number by another.
fn divide(input: DivideInput) -> Result[i64, str] {
  if input.b == 0 { .err("b is zero") } else { .ok(input.a / input.b) }
}

fn main() ! console, alloc {
  serve(stdio(), Info(name: "calc", version: "1")) fn(request) {
    request |> tool(divide)
  }
}
```

A client that lists the tools is told of `divide`, with "Divides one whole number by another." and a schema of two whole numbers, `a` and `b`, each with its comment. A client that calls `divide` with `{"a":6,"b":3}` is answered `2`.

## A tool

A tool takes one value, whose type has [`Deserialize`](deserialize.md), and gives a value whose type has [`Serialize`](serialize.md). The library reads the input from the call, runs the function, and writes what it gives, as text and as structured content. Nothing of the protocol is written in the tool.

**The description is the comment.** The comment on the function describes the tool, and the comment on a field of the input describes that field in the schema. A comment already belongs to the definition or the field that follows it, so there is one place to say what a thing is, for a reader of the code and for the model that calls the tool. A comment reaches the executable only for a function given to `tool` and the types it takes and gives.

**The result says how a tool fails.** A tool gives `T` or `Result[T, E]`. `.ok(v)` is the result `v`, and `.err(e)` is a result marked as an error that holds `e`, which the model reads and may act on. An input that does not fit the type never reaches the function: the library answers that the parameters are invalid, with the path and the problem of the [`ReadError`](deserialize.md). A call of a tool the server has not is answered by the library too.

## The tools of a server

`serve` is given a lambda, and every request goes through it. `tool(f)` answers a request that calls `f`, adds `f` to the answer of a request that lists the tools, and hands on any other, so the tools are a [pipeline](../language/pipeline.md).

```
serve(stdio(), info) fn(request) {
  request |> tool(add) |> tool(divide) |> tool(fetch)
}
```

No tool is stored in a value. A tool may have any effects, and they are required of the function the lambda is written in, as [the effects of a function that is passed](../language/effects.md#a-lambdas-effects) are: a server with a tool that carries `net` has a `main` that carries `net`. Which tool a call reaches is settled when the program is built, and a call of a tool is a direct call.

`tool` is a [function over code](../language/code.md), which is how it reads the name and the comment of the function it is given.

## Where a server listens

The first argument of `serve` is where requests come from and answers go. `stdio()` reads lines from the standard input and writes them to the standard output. The lambda and the tools are the same wherever the server listens. `worker()` takes each request from the host of a [WebAssembly module](../compiler.md#the-host-of-a-webassembly-module) and gives it the answer, which is how a server runs on a platform that calls it for each HTTP request, and its two operations are an effect, so they are imports of the module like any other.

## The forms

| Written | Is |
| --- | --- |
| `serve(stdio(), info) fn(request) { ... }` | A server that answers each request with what the lambda gives |
| `request \|> tool(f)` | The request answered by `f` when it calls `f`, and handed on otherwise |
| `Info(name: "calc", version: "1")` | What the server says of itself |
| `// ...` on a tool or a field of its input | The description a client is given |
| `-> Result[T, E]` on a tool | `.err(e)` is a result marked as an error |
