---
type: specification
title: HTTP server
description: std.http, with which a program answers HTTP requests with plain functions, each given its parameters from the path, the query, and the body, Respond, by which what a function gives becomes the response, and at, which puts a server of messages at a path.
status: partial
built:
  - "`std.http`, with `Request`, `Response`, `Status` and the errors `NotFound`, `BadRequest`, and `Unauthorized` with 404, 400, and 401, and the readers of a request's parts: `segments`, `route_match`, which gives each `:name` part of a path that a pattern fits, and `query_parts`"
  - "`Respond`, whose function `respond(value)` gives a `Response`: `Serialize` gives it for every type with `Serialize`, as JSON with status 200 and the header `content-type: application/json`, and the library writes `impl`s for `Html` (status 200, `text/html; charset=utf-8`), `Text` (200, `text/plain; charset=utf-8`), `Redirect` (302 with the header `location`), `()` (204, no body), and `Result[T, E]` for a `T` with `Respond` and an `E` with `Status` and `Serialize`, which is the response of the value, or status `status(e)` with `e` as JSON; a type's own `impl` is the one used, as [Traits](../language/traits.md#when-two-impls-fit) says"
  - "not `routes`, `get` and the other methods, `at`, a guard, `listen`, or the reading of a percent sign in a part"
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

What a function gives says what the response is, through the trait `Respond`.

```
trait Respond[T] {
  fn respond(value: T) -> Response ! alloc
}
```

| A function gives | The response is |
| --- | --- |
| A struct, an enum, a list, a number, or any type with [`Serialize`](serialize.md) | Status 200, with the value as JSON |
| An [`Html`](html.md) | Status 200, with the page |
| `Text(text: "...")` | Status 200, with the text as it is |
| `Redirect(to: "/login")` | A redirect to that place |
| `()` | Status 204, with no body |

`Respond` is given to every type that has `Serialize`, as JSON, in the way `Serialize` is given to a type whose parts have it. A type's own `impl` takes the place of the one it is given, which is how `Html`, `Text`, `Redirect`, and `()` answer as they do, and how a type of a program's own answers as it likes: a type that holds a picture writes an `impl` that gives its bytes and says what they are. Nothing of the library's types is special.

```
fn user(id: Nat) -> Result[User, NotFound] ! store

fn notes_page(user: User) -> Html ! store

fn logout() -> Redirect ! store
```

A function that can fail gives a `Result[T, E]`, and the type of the error says its status, through the trait `Status`.

```
use std.bits (u16)

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

`.ok(v)` is the response of `v`, by `Respond`. `.err(e)` is a response of status `status(e)` whose body is `e` as JSON. An error that is an enum says the status of each case with a `match`. A route whose function gives an error with no `Status` is refused when the program is built, so no failure is answered with a status nobody chose. The library has `NotFound`, `BadRequest`, and `Unauthorized`, with 404, 400, and 401.

A function of a route is a plain function. It names nothing of HTTP unless it gives one of the library's errors, so it is called from other code and tested as any function is, and the same function may be a tool of an [MCP](mcp.md) server.

## A server of messages at a path

`at` puts what follows it at a path. The block it is given asks for messages through the effect `messages`, as an [MCP](mcp.md) server does, and `at` answers that effect from the requests to its path: [`message`](mcp.md#where-the-messages-come-from) is the body of the request with its headers, and what `answer` is given is the response, with the status it says. `at` reads nothing of the message and checks nothing of the protocol. A request to the path that is not a POST is answered 405.

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

## Who may call

A guard decides, before a route runs, whether the request may reach it. It is written with [`with`](../language/with.md), and the routes after it in its block are the ones it guards.

```
use std.http (Caller, bearer, caller, get, identity, routes)

fn check(token: str) -> Option[Caller] ! store

fn health() -> str { "ok" }

fn my_notes() -> List[Note] ! identity, store {
  notes_of(caller().subject)
}

fn main() ! http, store, alloc {
  with routes()
  get("/health", health)
  with bearer(check)
  get("/notes", my_notes)
}
```

`/health` is answered for anyone. A request to `/notes` reaches `my_notes` only when it carries `Authorization: Bearer` with a token for which `check` gives `.some`. Any other request to it is answered 401, and `my_notes` does not run.

```
effect identity {
  fn caller() -> Caller
}

struct Caller {
  subject: str
  scopes: List[str]
}

fn bearer[T](check: fn(token: str) -> Option[Caller], body: fn() -> T ! identity) -> T
```

- **Who is calling is an effect.** `bearer` answers `identity` for the routes it guards, with the `Caller` that `check` gave. A function that needs to know who is calling carries `identity` and calls `caller()`.
- **A route that is not guarded cannot ask.** `get("/notes", my_notes)` written before the `with bearer(check)` line is refused when the program is built, because `my_notes` carries `identity` and nothing there answers it. So a route that needs a caller cannot be left open by mistake.
- **A guard is a function,** and a program writes its own in the same way: one that answers `identity` from a cookie, or one that asks for a scope and answers 403 without it.

## Where the requests come from

`routes` asks for each request and gives each response through the effect `http`, and does not know where they come from. What answers `http` is the deployment and not the program. A native executable, for every system Smith builds for, listens on a socket. In a [WebAssembly module](../compiler.md#the-host-of-a-webassembly-module) the operations of `http` are imports, which a host that is called for each request supplies. A test answers `http` with a handler of its own that gives the requests it wants, so a server is tested with no network. The source is the same for all of them, and what a deployment needs told, as the port a socket listens on, is said in `project.smith` and not in the program, as [Deployments](../language/deployments.md) says.

```
fn listen[T](port: Nat, body: fn() -> T ! http) -> T ! net, alloc
```

`listen` is the handler of a native executable. It answers `http` for the block it is given by listening on the port, through `net`, and is installed with `with listen(port: 8080)`.

## The parts of a request

A request is the method, the path and the query apart, the headers, and the body.

```
struct Request {
  method: str
  path: str
  query: str
  headers: List[(str, str)]
  body: str
}
```

`route_match(pattern, path)` gives `.some` of each part of the path that a `:name` part of the pattern stands at, as a `Part` of the name and the text, when the pattern and the path have the same number of parts and each part that does not begin with `:` is the same, and `.none` when they do not. `route_match("/users/:id", "/users/7")` is one `Part`, `id` with `7`, and `/users` does not fit it. `query_parts("a=1&b=&c")` gives `a` with `1`, `b` with the empty text, and `c` with the empty text: the members are the pieces between `&`, each cut at its first `=`, and a piece with nothing in it is no member. A part is read as the type of its parameter by [`from_text`](fromtext.md).

## The forms

| Written | Is |
| --- | --- |
| `with routes()` | A server whose routes are the lines that follow |
| `get("/users/:id", user)` | `user` answers `GET` of that path, with `id` read from it |
| `post`, `put`, `patch`, `delete` | The same for each method |
| `fn f(new: NewUser)` | A parameter read from the body, as JSON |
| `fn f(text: str, limit: u64 = 20)` | Parameters read from the query |
| `-> T`, `-> ()` | Status 200 with `T` as JSON, and 204 |
| `-> Html`, `-> Text`, `-> Redirect` | A page, a text, and a redirect |
| `impl Respond[T]` | How a type of the program's own answers |
| `-> Result[T, E]` | `.err(e)` is the status `Status` gives for `e`, with `e` as JSON |
| `with bearer(check)` | The routes after it, reached only with a token `check` accepts, with `identity` answered for them |
| `caller()` | Who sent the request, in a function that carries `identity` |
| `at("/mcp") { ... }` | What asks for messages, put at a path |
