---
type: specification
title: HTTP server
description: std.http, with which a program answers HTTP requests with plain functions, each given its parameters from the path, the query, and the body, and at, which puts a server of messages at a path.
status: planned
---

# HTTP server

`std.http` makes a program a server of HTTP. A route is a method, a path, and a plain function that answers it. The function's parameters are read from the request, and what it gives is the response.

```
use std.http (routes, get, post, Status, NotFound)

struct User {
  id: u64
  name: str
}

struct NewUser {
  name: str
}

fn user(id: u64) -> Result[User, NotFound] ! store

fn create_user(new: NewUser) -> User ! store

fn main() ! http, store, alloc {
  with routes()
  get("/users/:id", user)
  post("/users", create_user)
}
```

`GET /users/7` calls `user(7)` and answers with the user as JSON. `POST /users` with the body `{"name":"Ana"}` calls `create_user` with a `NewUser` read from it. `routes` is written with [`with`](../language/with.md), so the lines after it are the routes, and they are run for each request.

## Where a parameter comes from

- A parameter whose name stands after a `:` in the path is read from that part of the path. `get("/users/:id", user)` reads `id` from the second part, as the type of the parameter, so `/users/x` does not fit a `u64`.
- A parameter that is a struct, an enum, a list, or a tuple is read from the body, as JSON. A function has at most one.
- Every other parameter is read from the query, by its name, and an `Option` or a parameter with a default may be left out, so `fn search(text: str, limit: u64 = 20)` answers `/search?text=a`.

A path that names a part the function has no parameter for is refused when the program is built, and so is a function with two parameters for the body. The compiler does not build `std.http`, so no words are quoted.

A request whose parts do not fit the parameters never reaches the function. The library answers 400, with the path and the problem of the [`ReadError`](deserialize.md) as JSON. A request that fits no route is answered 404.

## What a function gives

What a function gives has [`Serialize`](serialize.md), and is the body of a response of status 200, as JSON. A function that gives `()` answers 204 with no body.

A function that can fail gives a `Result[T, E]`, and the type of the error says its status, through the trait `Status`.

```
trait Status[E] {
  fn status(e: E) -> u16
}

struct Taken {
  name: str
}

impl Status[Taken] {
  fn status(e: Taken) -> u16 { 409 }
}
```

`.err(e)` is a response of status `status(e)` whose body is `e` as JSON. An error that is an enum says the status of each case with a `match`. A route whose function gives an error with no `Status` is refused when the program is built, so no failure is answered with a status nobody chose. The library has `NotFound`, `BadRequest`, and `Unauthorized`, with 404, 400, and 401.

A function of a route is a plain function. It names nothing of HTTP unless it gives one of the library's errors, so it is called from other code and tested as any function is, and the same function may be a tool of an [MCP](mcp.md) server.

## A server of messages at a path

`at` puts what follows it at a path. The block it is given asks for messages through the effect `messages`, as an [MCP](mcp.md) server does, and `at` answers that effect from the requests to its path.

```
fn main() ! http, store, alloc {
  with routes()
  get("/health", health)
  at("/mcp") {
    with mcp(name: "notes", version: "1")
    tool(add_note)
    tool(list_notes)
  }
}
```

`mcp` is the function it is when it stands alone. It does not know of `at`, and `at` does not know of MCP: anything that asks for messages is put at a path the same way.

## Where the requests come from

`routes` asks for each request and gives each response through the effect `http`, and does not know where they come from. What answers `http` is the deployment and not the program. A native executable, for every system Smith builds for, listens on a socket. In a [WebAssembly module](../compiler.md#the-host-of-a-webassembly-module) the operations of `http` are imports, which a host that is called for each request supplies. A test answers `http` with a handler of its own that gives the requests it wants, so a server is tested with no network. The source is the same for all of them, and what a deployment needs told, as the port a socket listens on, is said in `project.smith` and not in the program.

## The forms

| Written | Is |
| --- | --- |
| `with routes()` | A server whose routes are the lines that follow |
| `get("/users/:id", user)` | `user` answers `GET` of that path, with `id` read from it |
| `post`, `put`, `patch`, `delete` | The same for each method |
| `fn f(new: NewUser)` | A parameter read from the body, as JSON |
| `fn f(text: str, limit: u64 = 20)` | Parameters read from the query |
| `-> T`, `-> ()` | Status 200 with `T` as JSON, and 204 |
| `-> Result[T, E]` | `.err(e)` is the status `Status` gives for `e`, with `e` as JSON |
| `at("/mcp") { ... }` | What asks for messages, put at a path |
