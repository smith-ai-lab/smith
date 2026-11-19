---
type: specification
title: OAuth
description: std.oauth, with which a server is an OAuth resource server, as a Model Context Protocol server on the network is: it says which authorization server to use and lets in only a request with a token from it.
status: planned
---

# OAuth

The module `std.oauth` makes an HTTP server an OAuth resource server: a server that issues no token and lets in only a request that carries one from an authorization server it names. It is what the Model Context Protocol asks of a server on the network, and it is one [guard](http.md#who-may-call).

```
use std.http (at, get, routes)
use std.jwt (Key)
use std.mcp (mcp, tool)
use std.oauth (oauth)

fn main(key: Key) ! http, time, store, alloc {
  with routes()
  get("/health", health)
  with oauth(
    issuer: "https://login.example.com",
    audience: "https://notes.example.com/mcp",
    key: key,
  )
  at("/mcp") {
    with mcp(name: "notes", version: "1")
    tool(add_note)
  }
}
```

The server of messages at `/mcp` is reached only with a token that `https://login.example.com` signed for `https://notes.example.com/mcp`. Nothing of this is written in `mcp` or in a tool, as [MCP](mcp.md#who-may-call) says.

## What oauth does

`oauth` is written with [`with`](../language/with.md) and guards the routes after it, as `bearer` does. It does three things.

- **It says where to get a token.** It answers `/.well-known/oauth-protected-resource`, with the document of RFC 9728 that names the audience as the resource and the issuer as its authorization server, so a client that knows nothing of the server finds where to ask.
- **It turns away a request with no token that is right.** Such a request is answered 401, with a `WWW-Authenticate` header that gives the address of that document. A token is right when [`verify`](jwt.md) gives its claims for the key, the issuer, and the audience. A token for another audience is not right, so one meant for another server is not taken.
- **It answers `identity`.** For a request it lets in, [`caller()`](http.md#who-may-call) is the subject and the scopes of the token.

`oauth` reads no code and knows nothing of what it guards: it guards an HTML page as it guards a server of messages.

## Scopes

`needs` guards the routes after it with a scope, and answers 403, with the scope that is missing, to a caller whose token has not got it.

```
use std.oauth (needs, oauth)

fn main(key: Key) ! http, time, store, alloc {
  with routes()
  with oauth(issuer: "https://login.example.com", audience: "https://notes.example.com", key: key)
  get("/notes", my_notes)
  with needs("notes:write")
  post("/notes", add_note)
}
```

`needs` carries `identity`, so it is written after a guard that answers it and is refused before one.

## What is not here

- The key is given to `oauth`. Reading the keys of an authorization server from its address, as most servers do, needs a request out to it, which no effect of the library makes yet.
- A key is a [`Key`](jwt.md) of `std.jwt`, which has the one case it has.
- `std.oauth` is not an authorization server: it signs nothing, registers no client, and shows no page to a user.

## The forms

| Written | Is |
| --- | --- |
| `with oauth(issuer: i, audience: a, key: k)` | The routes after it, reached only with a token from `i` for `a`, with `identity` answered |
| `with needs(scope)` | The routes after it, reached only by a caller whose token has the scope |
