---
type: specification
title: Requests to another server
description: The fetch effect of std.fetch, by which a program sends an HTTP request to another server and gets its response, and what answers it natively and in a WebAssembly module.
status: planned
---

# Requests to another server

The module `std.fetch` declares the `fetch` [effect](../language/effect-declarations.md), by which a program asks another server for something over HTTP. A program says what it asks, and the deployment says how the request travels, as it does for [`http`](http.md), which is the other direction.

```
use std.fetch (FetchError, Request, fetch, send)

fn keys_of(issuer: str) -> Result[str, FetchError] ! fetch, alloc {
  response = try send(Request(url: "{issuer}/.well-known/jwks.json"))
  if response.status == 200 { .ok(response.body) } else { .err(.failed("status {response.status}")) }
}
```

`keys_of("https://login.example.com")` asks that server for the document at that path and gives its text, or what went wrong.

## The effect

```
effect fetch {
  fn send(request: Request) -> Result[Response, FetchError]
}

struct Request {
  url: str
  method: Method = .get
  headers: List[(str, str)] = []
  body: str = ""
}

struct Response {
  status: 100..600
  headers: List[(str, str)]
  body: str
}

enum Method {
  get
  post
  put
  delete
  patch
  head
}

enum FetchError {
  address: str
  unreachable: str
  untrusted: str
  failed: str
}
```

- `send` gives the response of the server, whatever its status: a 404 is `.ok` of a response whose status is 404. It gives `.err` only when there is no response to give.
- `address` is a `url` that is not one. `unreachable` is a server that could not be found or did not answer. `untrusted` is a server reached over `https` whose certificate does not show it is the server the address names. `failed` is anything else, with what was said.
- **An address that begins `https` is always checked.** No argument and no setting sends a request to a server whose certificate is not right, so `untrusted` cannot be turned off.
- A response that sends the program to another address is given as it is, with its status and its `location`, and `send` follows nothing.

## What answers it

What answers `fetch` is the [deployment](../language/deployments.md) and not the program.

- A test answers it with a handler of its own that gives the responses it wants, so a program that asks other servers is tested with no network.
- In a [WebAssembly module](../compiler.md#the-host-of-a-webassembly-module) `send` is an import, and the host makes the request. On [Cloudflare](cloudflare.md) that is the Worker's own way of asking, so a deployment there leaves `fetch` to it, as it leaves `http`.
- A native executable answers it with a handler written in Smith over `net`. For an address that begins `http` it writes the request to a socket. For one that begins `https` it speaks TLS 1.3, which is written in Smith in the library, on [`std.sha2`](sha2.md) and the arithmetic of [wide numbers](../language/arithmetic.md#more-than-64-bits), and it trusts the certificates the system trusts. Nothing of the system's own TLS, and no library that is not Smith, is called.

## The forms

| Written | Is |
| --- | --- |
| `send(Request(url: u))` | The response of the server at `u`, or why there is none |
| `Request(url: u, method: .post, headers: h, body: b)` | A request with a method, headers, and a body |
