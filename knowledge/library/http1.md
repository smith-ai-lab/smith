---
type: specification
title: HTTP/1.1 messages
description: std.http1, which reads and writes the messages of HTTP/1.1 as bytes: the head of a request and of a response, and a body by its length or in chunks, with nothing read that the standard says to refuse.
status: planned
---

# HTTP/1.1 messages

The module `std.http1` turns bytes into the messages of HTTP/1.1 and back, as RFC 9112 writes them. It carries no effect but `alloc`: it is given bytes and gives values, and knows nothing of sockets. What reads the bytes from a connection is a handler of [`http`](http.md) or of [`fetch`](fetch.md).

```
use std.http1 (Read, request_head)

fn first_line(bytes: Slice[Byte]) -> str ! alloc {
  match request_head(bytes) {
    .complete(head, used) { "{head.method} {head.target}" }
    .incomplete { "more bytes are needed" }
    .refused(why) { why }
  }
}
```

Given the bytes of `GET /notes?limit=3 HTTP/1.1`, a line break, `host: example.com`, and two line breaks, `first_line` is `"GET /notes?limit=3"`. Given the same bytes without the last line break it is `"more bytes are needed"`.

## Reading

```
enum Read[T] {
  complete: (T, Nat)
  incomplete
  refused: str
}

struct RequestHead {
  method: str
  target: str
  headers: List[(str, str)]
}

struct ResponseHead {
  status: 100..600
  reason: str
  headers: List[(str, str)]
}

enum Body {
  none
  length: Nat
  chunked
}

fn request_head(bytes: Slice[Byte]) -> Read[RequestHead] ! alloc
fn response_head(bytes: Slice[Byte]) -> Read[ResponseHead] ! alloc
fn body_of(headers: List[(str, str)]) -> Result[Body, str]
fn chunk(bytes: Slice[Byte]) -> Read[Slice[Byte]]
```

- `complete` holds the value and how many of the bytes it took, so the caller goes on from there. `incomplete` says the bytes so far are right and there are not enough of them. `refused` says the bytes are no message, with why, and more bytes will not make them one.
- A head is read only when it is whole. The name of a header is given in small letters, and its value with the space at its ends taken off.
- `body_of` says how the body that follows a head is framed, from its headers. `chunk` reads one chunk and gives its bytes, as a piece of what it was given. A chunk of no bytes is the last.
- A head is refused where RFC 9112 says a server must not accept it, as these let one request be read as two by two servers that read it differently: a line that ends with a line feed alone, a space before the colon of a header, a header line that continues the one before, a byte that is no part of a name or a value, more than one `content-length` or one that is not a number, and `content-length` together with `transfer-encoding`.
- Nothing is read without a limit. A head of more bytes than `limit`, which each function takes with a default of 16384, is refused, so a peer that never ends a line cannot fill memory.

## Writing

```
fn write_request_head(to: take Text, head: RequestHead) -> Text ! alloc
fn write_response_head(to: take Text, head: ResponseHead) -> Text ! alloc
fn write_chunk(to: take List[Byte], bytes: Slice[Byte]) -> List[Byte] ! alloc
```

Each adds what it writes to what it is given, and gives it back. A head is text, so it is added to a [`Text`](text.md#building-a-text). A name or a value that holds a line break is not written: the two head functions are refused such a value by a `requires`, so a header that came from outside cannot add a line of its own.

For every head `h` this module writes, reading what it wrote gives `h`.

## The forms

| Written | Gives |
| --- | --- |
| `request_head(bytes)`, `response_head(bytes)` | The head and how many bytes it took, or that more are needed, or why it is refused |
| `body_of(headers)` | How the body is framed: none, a length, or chunks |
| `chunk(bytes)` | The bytes of one chunk |
| `write_request_head(to, head)`, `write_response_head(to, head)`, `write_chunk(to, bytes)` | What was given, with the message added |
