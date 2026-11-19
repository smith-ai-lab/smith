---
type: specification
title: JSON-RPC
description: std.jsonrpc, which reads and writes the messages of JSON-RPC 2.0, a request, a notification, and a response, as values, with the parameters and results left as RawJson.
status: partial
built:
  - "`std.jsonrpc`, written in Smith, with `Id`, `Request`, `Notification`, `Failure`, `Response`, `Message`, and `Received`, `read`, `write`, `write_batch`, and the five failures the standard names, which reads an object, a batch, and each member of a batch on its own, and never gives a message the standard does not allow"
  - "not `use std.jsonrpc (read)` or `(write)` as the example writes it: a name a program lists leaks into the library modules it reaches, so it is refused, and the module is imported as `use std.jsonrpc` with `jsonrpc.read(text)`"
---

# JSON-RPC

The module `std.jsonrpc` reads and writes the messages of JSON-RPC 2.0, the form in which one program asks another to run something by name. The Model Context Protocol and the Language Server Protocol both send such messages, so the module is on its own and [`std.mcp`](mcp.md) is built on it. It knows nothing of how a message travels: a line, the body of an HTTP request, or bytes after a header are the business of whoever carries it.

## A message

```
enum Id {
  number: Int
  text: str
  null
}

struct Request {
  id: Id
  method: str
  params: Option[RawJson]
}

struct Notification {
  method: str
  params: Option[RawJson]
}

struct Failure {
  code: Int
  message: str
  data: Option[RawJson]
}

struct Response {
  id: Id
  outcome: Result[RawJson, Failure]
}

enum Message {
  request: Request
  notification: Notification
  response: Response
}
```

- A `Request` asks for `method` to be run and is answered by one `Response` with the same `id`. A `Notification` asks and is never answered.
- `params` and the result of a `Response` are [`RawJson`](json.md#rawjson): the text of one JSON value, not taken apart. Whoever knows the method reads it into its own type with `from_json`, so this module needs no type of any protocol built on it.
- An `Id` is what the asker chose. `.null` is the id of a `Response` to a request whose id could not be read.
- A `Failure` is how a request fails: a code, a sentence, and anything more the answerer wants to say.

## Reading

```
enum Received {
  one: Message
  many: List[Result[Message, Failure]]
}

fn read(text: str) -> Result[Received, Failure] ! alloc
```

`read` takes the text of one JSON value. An object is one message. An array is a batch, and each member is read on its own, so one bad member does not lose the rest.

```
use std.jsonrpc (Message, Received, read)

fn method_of(text: str) -> str ! alloc {
  match read(text) {
    .ok(.one(.request(r))) { r.method }
    .ok(.one(.notification(n))) { n.method }
    _ { "" }
  }
}
```

`method_of("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}")` is `"ping"`.

`read` never gives a message that the standard does not allow, and what it refuses is a `Failure` ready to send back:

- Text that is not JSON is `.err` of the failure `parse_error()`.
- A value that is no message is `.err` of `invalid_request()`: one with no `"jsonrpc": "2.0"`, a `method` that is not a text, an `id` that is not a text, a whole number, or `null`, `params` that are neither an object nor an array, a response with both a result and an error or with neither, and an array with nothing in it.
- An id written with a fraction or an exponent is refused, as two programs do not agree on when such numbers are equal.

## Writing

```
fn write(to: take Text, message: Message) -> Text ! alloc
fn write_batch(to: take Text, messages: List[Message]) -> Text ! alloc
```

`write` appends one message to a [`Text`](text.md), with `"jsonrpc":"2.0"` first, and `write_batch` appends several as one array. For every message `m`, reading what `write` wrote gives `m`.

## The failures the standard names

```
fn parse_error() -> Failure
fn invalid_request() -> Failure
fn method_not_found(method: str) -> Failure ! alloc
fn invalid_params(why: str) -> Failure ! alloc
fn internal_error() -> Failure
```

Each gives the `Failure` with the code the standard gives it, -32700, -32600, -32601, -32602, and -32603. The codes from -32768 to -32000 are the standard's, and a protocol built on it chooses its own outside them.

## The forms

| Written | Is |
| --- | --- |
| `read(text)` | One message, or a batch with each member read on its own, or the `Failure` to send back |
| `write(to, m)`, `write_batch(to, ms)` | The message, or the batch, appended to a `Text` |
| `Request`, `Notification`, `Response` | The three messages |
| `Id` | A whole number, a text, or `null` |
| `parse_error()`, `invalid_request()`, `method_not_found(m)`, `invalid_params(why)`, `internal_error()` | The failures the standard names |
