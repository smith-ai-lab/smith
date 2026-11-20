---
type: specification
title: MCP
description: std.mcp, with which a program is a Model Context Protocol server whose tools, resources, and prompts are plain functions, each described by its own comment and its parameters.
status: planned
---

# MCP

`std.mcp` makes a program a server of the Model Context Protocol. A tool is a plain function. Its name is the function's name, its description is the comment on the function, and the schema of its input is made from its parameters.

```
use std.mcp (mcp, tool)

// Divides one whole number by another.
fn divide(
  // The number that is divided.
  a: Int,
  // The number it is divided by.
  b: Int,
) -> Result[Int, str] {
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

**The result says how a tool fails.** A tool gives `T` or `Result[T, E]`. `.ok(v)` is the result `v`, and `.err(e)` is a result marked as an error that holds `e`, which the model reads and may act on. An input that does not fit the parameters never reaches the function: the library answers that the parameters are invalid, with the path and the problem of the [`ReadError`](deserialize.md). A call of a tool the server has not is answered by the library too, with the failure -32602, as an input that does not fit is.

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

The server speaks the version of the protocol dated 2026-07-28, which has no handshake and keeps nothing between two requests. A client that sends `server/discover` is told the versions the server speaks, the capabilities its lines imply, and the instructions `mcp` was given, if any, and every result carries the server's name and version. A request that names a version the server does not speak is answered with the failure -32022 and the versions it does, and a client of an earlier version that begins with `initialize` is answered the same way.

No tool is stored in a value. A tool may have any effects, and they are required of the function its name is written in, as [the effects of a function that is passed](../language/effects.md#a-lambdas-effects) are: a server with a tool that carries `net` has a `main` that carries `net`, so the first line of `main` says everything the server can reach. Which tool a message calls is settled when the program is built, and a call of a tool is a direct call.

`tool` takes a function that has [`Function`](../language/traits.md#an-impl-for-a-function), which every function with a name is given, and that is how it has the function's name, its comment, and its parameters with theirs: it reads the arguments struct from the message with [`Deserialize`](deserialize.md), calls the function, and describes the struct with [`Describe`](describe.md) for a client that lists the tools. `tool` reads no code. `tool` carries an effect that `mcp` handles, which is how a line with no value to pass along reaches the message being answered.

## A resource

A resource is something a client reads by its address. It is a plain function too: `resource` takes the pattern of the address and the function that gives what is there.

```
use std.mcp (mcp, resource, tool)

// One note, by its number.
fn note(id: Nat) -> Result[Note, NotFound] ! store

// Every note there is.
fn notes() -> List[Note] ! store

fn main() ! messages, store, alloc {
  with mcp(name: "notes", version: "1")
  resource("notes://all", notes)
  resource("notes://note/:id", note)
  tool(add_note)
}
```

A client that reads `notes://note/7` is given what `note(7)` gives. A part of the pattern that begins with `:` is a parameter of the function, by its name, read as the parameter's type, as a part of a path is for a [route](http.md#where-a-parameter-comes-from). A pattern with no such part is one resource, and one with any is every resource that fits it, which a client is told of as a template with the part in braces, `notes://note/{id}`.

The description is the comment, as for a tool. What the function gives has [`Serialize`](serialize.md) and is written as JSON, or is a `str` and is written as it is. `.err(e)` of a `Result` is answered as a resource that is not there, the failure -32602 with the address, and an address that fits no pattern, or whose part does not read as its parameter's type, is answered by the library and never reaches a function.

## A prompt

A prompt is a text a client offers its user, with blanks the user fills. It is a function that takes the blanks and gives the text.

```
use std.mcp (mcp, prompt)

// Asks for a summary of one note at a length.
fn summarise(
  // The number of the note.
  id: Nat,
  // How many sentences the summary may have.
  sentences: Nat = 3,
) -> Result[str, NotFound] ! store {
  n = try note(id)
  .ok("Summarise this note in {sentences} sentences:\n{n.text}")
}

fn main() ! messages, store, alloc {
  with mcp(name: "notes", version: "1")
  prompt(summarise)
}
```

A client that lists the prompts is told of `summarise`, with its comment and its two blanks, of which `sentences` may be left out. A client that asks for it with `id` as 7 is given the text, as one message from the user. The parameters are read as a tool's are, and the comment on each describes its blank.

`resource` and `prompt` are lines beside `tool`, are bounded by [`Function`](../language/traits.md#an-impl-for-a-function) as it is, and read no code. A server offers what its lines say, so one with no `resource` line tells a client it has no resources, and a request to list or read one is answered that the method is not found, -32601.

## Who may call

`std.mcp` checks no one. Who may reach a server is a question about the request that carried the message, and so it belongs to what carries it: a server a client starts on the same machine is reached only by that client, and a server on the network is behind whatever [`std.http`](http.md) does to a request before it reaches [`at`](http.md#a-server-of-messages-at-a-path). Nothing of authorization is written in a tool, a resource, or a prompt.

## Where the messages come from

Each message is a message of JSON-RPC 2.0, read and written by [`std.jsonrpc`](jsonrpc.md), and `std.mcp` reads the parameters of each method into its own types.

```
struct Incoming {
  text: str
  headers: List[(str, str)]
}

struct Reply {
  text: Option[str]
  status: 100..600
}

effect messages {
  fn message() -> Incoming
  fn answer(reply: Reply)
}
```

- `message` gives the message being answered, with what carried it knows of it: the headers of the request it came in, each name in small letters, and none for a message that came by the standard input.
- `answer` takes what `mcp` says of it: the text of one message, or none for a message that needs no answer, and a status. Whoever carries messages over HTTP sends that status, and whoever carries them another way has no use for it.

The protocol asks a server to hold the headers of a request to the message inside it, and to choose the status by how the message fails, so `mcp` does both and what carries the message knows nothing of the protocol. `mcp` refuses with status 400 a message whose `Mcp-Method` header is not its method, whose `Mcp-Name` header is not the tool, the prompt, or the address it names, or whose `MCP-Protocol-Version` header is not the version it says inside, each only where there are headers. It answers 404 for a method it has not, 202 with no text for a message that needs no answer, and 200 for every other answer.

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
| `resource("notes://note/:id", f)` | What `f` gives, read at each address that fits the pattern |
| `prompt(f)` | The text `f` gives, offered with its parameters as blanks |
| `// ...` on a tool, a resource, a prompt, a parameter, or a field | The description a client is given |
| `-> Result[T, E]` on a tool | `.err(e)` is a result marked as an error |
| `at("/mcp") { with mcp(...) ... }` | The server at a path of an HTTP server |
