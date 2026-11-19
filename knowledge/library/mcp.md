---
type: specification
title: MCP
description: std.mcp, with which a program is a Model Context Protocol server whose tools are plain functions, each described by its own comment and its parameters.
status: planned
---

# MCP

`std.mcp` makes a program a server of the Model Context Protocol. A tool is a plain function. Its name is the function's name, its description is the comment on the function, and the schema of its input is made from its parameters.

```
use std.mcp (mcp, tool)

// Divides one whole number by another.
fn divide(
  // The number that is divided.
  a: i64,
  // The number it is divided by.
  b: i64,
) -> Result[i64, str] {
  if b == 0 { .err("b is zero") } else { .ok(a / b) }
}

fn main() ! messages, alloc {
  with mcp(name: "calc", version: "1")
  tool(divide)
}
```

A client that lists the tools is told of `divide`, with "Divides one whole number by another." and a schema of two whole numbers, `a` and `b`, each with its comment. A client that calls `divide` with `{"a":6,"b":3}` is answered `2`.

## A tool

A tool is a function with a name. Each parameter is a member of the object a client sends, read with [`Deserialize`](deserialize.md), so a parameter may be a number, a text, a struct, a list, or an `Option`, which a client may leave out, and one with a default may be left out too. What the function gives has [`Serialize`](serialize.md), and is written as text and as structured content. Nothing of the protocol is written in the tool, and the same function is called from other code as any function is.

**The description is the comment.** The comment on the function describes the tool, the comment on a parameter describes that member, and the comment on a field of a struct describes the field. A comment belongs to what follows it, as [Functions](../language/functions.md#comments) says, so there is one place to say what a thing is, for a reader of the code and for the model that calls the tool. A comment reaches the executable only for a function given to `tool` and the types it takes and gives.

**The result says how a tool fails.** A tool gives `T` or `Result[T, E]`. `.ok(v)` is the result `v`, and `.err(e)` is a result marked as an error that holds `e`, which the model reads and may act on. An input that does not fit the parameters never reaches the function: the library answers that the parameters are invalid, with the path and the problem of the [`ReadError`](deserialize.md). A call of a tool the server has not is answered by the library too.

## The tools of a server

`mcp` is written with [`with`](../language/with.md), so the lines after it are what the server offers, and they are run for each message a client sends. `tool(f)` answers a message that calls `f`, adds `f` to the answer of one that lists the tools, and does nothing for any other.

```
fn main() ! messages, net, alloc {
  with mcp(name: "pages", version: "1")
  tool(add)
  tool(divide)
  tool(fetch_page)
}
```

`mcp` takes the name and the version of the server, and what else the protocol lets a server say of itself as arguments with defaults, so a server that says no more writes no more.

No tool is stored in a value. A tool may have any effects, and they are required of the function its name is written in, as [the effects of a function that is passed](../language/effects.md#a-lambdas-effects) are: a server with a tool that carries `net` has a `main` that carries `net`, so the first line of `main` says everything the server can reach. Which tool a message calls is settled when the program is built, and a call of a tool is a direct call.

`tool` takes a function that has [`Function`](../language/traits.md#an-impl-for-a-function), which every function with a name is given, and that is how it has the function's name, its comment, and its parameters with theirs: it reads the arguments struct from the message with [`Deserialize`](deserialize.md), calls the function, and describes the struct with [`Describe`](describe.md) for a client that lists the tools. `tool` reads no code. `tool` carries an effect that `mcp` handles, which is how a line with no value to pass along reaches the message being answered.

## Where the messages come from

`mcp` asks for each message and gives each answer through the effect `messages`, and does not know where they come from. What answers `messages` is the deployment and not the program:

- A server a client starts on the same machine reads each message from the standard input and writes each answer to the standard output.
- A server on the network takes each message from an HTTP request and gives the answer as its response, as the protocol's transport over HTTP says. In a [WebAssembly module](../compiler.md#the-host-of-a-webassembly-module) the operations of `messages` are imports, which the host supplies from the request it was called with.
- Beside other routes of one server, [`at`](http.md#a-server-of-messages-at-a-path) supplies the messages from a path.

The source is the same for all three.

## The forms

| Written | Is |
| --- | --- |
| `with mcp(name: "calc", version: "1")` | A server whose offers are the lines that follow |
| `tool(f)` | The function `f` offered as a tool |
| `// ...` on a tool, a parameter, or a field | The description a client is given |
| `-> Result[T, E]` on a tool | `.err(e)` is a result marked as an error |
| `at("/mcp") { with mcp(...) ... }` | The server at a path of an HTTP server |
