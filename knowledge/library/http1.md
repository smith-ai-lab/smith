---
type: specification
title: HTTP/1.1 messages
description: std.http1, which reads and writes the messages of HTTP/1.1 as bytes, and reads a request of HTTP/1.0: the head of a request and of a response, and a body by its length or in chunks, with nothing read that the standard says to refuse.
status: partial
built:
  - "`request_head`, `response_head`, `body_of`, `chunk`, and `write_chunk` of `std.http1`, written in Smith, with `Read`, `Version`, `RequestHead` with its `version`, `ResponseHead`, and `Body`, a request of `HTTP/1.0`, every refusal the page lists, the rules of RFC 9112 that bind a reader, and a `limit` of 16384 that each reader takes"
  - "not `write_request_head` or `write_response_head`, which need a `requires` over a text, and a contract is only a comparison of whole numbers"
---

# HTTP/1.1 messages

The module `std.http1` turns bytes into the messages of HTTP/1.1 and back, as RFC 9112 writes them. It carries no effect but `alloc`: it is given bytes and gives values, and knows nothing of sockets. What reads the bytes from a connection is a handler of [`http`](http.md) or of [`fetch`](fetch.md).

```
use std.http1 (Read, request_head)

fn first_line(bytes: Slice[Byte]) -> str ! alloc {
  match request_head(bytes) {
    .complete((head, used)) { "{head.method} {head.target}" }
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

enum Version {
  http10
  http11
}

struct RequestHead {
  method: str
  target: str
  version: Version
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

fn request_head(bytes: Slice[Byte], limit: Nat = 16384) -> Read[RequestHead] ! alloc
fn response_head(bytes: Slice[Byte], limit: Nat = 16384) -> Read[ResponseHead] ! alloc
fn body_of(headers: List[(str, str)]) -> Result[Body, str]
fn chunk(bytes: Slice[Byte], limit: Nat = 16384) -> Read[Slice[Byte]]
```

- `complete` holds the value and how many of the bytes it took, so the caller goes on from there. `incomplete` says the bytes so far are right and there are not enough of them. `refused` says the bytes are no message, with why, and more bytes will not make them one.
- A head is read only when it is whole. The name of a header is given in small letters, and its value with the space at its ends taken off.
- `body_of` says how the body that follows a head is framed, from its headers. `chunk` reads one chunk and gives its bytes, as a piece of what it was given. A chunk of no bytes is the last, and the trailers after it are passed over. The size may be followed by blanks and then by chunk extensions, which are ignored. `body_of` gives `.err` for a `content-length` that is not one number and for both framings together.
- A head is refused where RFC 9112 says a server must not accept it, as these let one request be read as two by two servers that read it differently: a line that ends with a line feed alone, a space before the colon of a header, a byte that is no part of a name or a value, more than one `content-length` or one that is not a number, `content-length` together with `transfer-encoding`, and whitespace between the start line and the first header. A header line that continues the one before is refused in a request, and in a response each is made one space, as RFC 9112 asks of a user agent. A request is read of `HTTP/1.1` and of `HTTP/1.0`, and a response of `HTTP/1.1`, and any other version is refused. A request of `HTTP/1.0` with a `transfer-encoding` is refused, as RFC 9112 says its framing is faulty. A request of `HTTP/1.1` is refused that has no `host` header, and a request of either version that has more than one, or one whose value is not a host: letters, digits, and the marks of a name, a colon, brackets, and `%` with two hexadecimal digits after it. A name or a value that is not UTF-8 is refused, as each is a `str`.
- `transfer-encoding` is read as the list of codings it is, over every line of it, and `body_of` says `.err` for one that is empty, or has `chunked` twice, or has another coding last. A response head whose last coding is not `chunked` is read, as its body then runs to the end of the connection, and `body_of` still says `.err` of it. A request head whose headers `body_of` refuses is refused.
- Nothing is read without a limit. A head, or a chunk, of more bytes than `limit`, which each of the three functions takes with a default of 16384, is refused, so a peer that never ends a line cannot fill memory.

## Where the standard leaves a choice

The readers take the strict side of each, so that two servers cannot read one message two ways.

- A line ends with a carriage return and a line feed, and a line feed alone is refused, though RFC 9112 lets a recipient take it. An empty line before the request line is refused, though a server should ignore one.
- A second `content-length` is refused even when it equals the first, and so is a value that is a list, as `3, 3`, though the standard lets a recipient take a list of equal values as one.
- A request of `HTTP/1.0` is read strictly: every refusal of a request of `HTTP/1.1` holds for it but the need for a `host`, and the `transfer-encoding` that it refuses. A version of another minor number, `HTTP/1.2`, is refused, though the standard lets a recipient take a later minor version as the one it knows.
- The value of `host` is judged by its bytes and not by the whole grammar of a URI. A value of a header that is not UTF-8 is refused, though the standard allows any byte from 128 on, as each value is a `str`.
- A response whose last transfer coding is not `chunked` is read, as its body ends where the connection does, and so is a response with neither a length nor a coding, which `body_of` says `.none` of. What the method or the status of a response says about its body, a `HEAD`, a `204`, or a `CONNECT`, is for what holds the connection, as `body_of` takes only headers.

## Answering a request of HTTP/1.0

`version` of a `RequestHead` says which version the request was. A client of `HTTP/1.0` is answered with no chunked body, and the connection is closed after the response unless the request asked to keep it with `connection: keep-alive`. These bind what writes the response, [`listen`](http.md), and not this module, which only reads the request and says what it was.

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
