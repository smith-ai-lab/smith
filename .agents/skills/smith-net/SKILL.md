---
name: smith-net
description: "Using std::net (import std::net; net::connect, connect_tls, listen, accept, read, read_within, write, close, stop, net::fake): TCP and TLS by direct calls that return a result, with a scripted in-memory fake for tests. Load when code imports std::net or includes <smith/net.h>, or on opening a socket, a TLS connection or a server."
---

# TCP and TLS by direct calls: std::net

Call `net::` anywhere. A call returns `Result(T, net::Error)`; the error is what the world said (`NotFound` for a name that does not resolve, `Refused`, `Unreachable`, `TimedOut`, `Reset`, `InUse`, `Denied`, `TooMany`; for TLS `Expired`, `NotYetValid`, `WrongHost`, `Untrusted`, `BadCertificate`, `HandshakeFailed`, `Unsupported` when the system has no TLS stack; `Other`). A host is a name or an address (`const char host[:]`). The calls block, take your buffers and allocate nothing.

```smith
module scratch;
import std::net;
export Usize fetch(const U8 request[:], U8 reply[:]) {
    net::ConnectionResult made = net::connect_tls("example.com", 443);
    switch (made) {
    case .error:
        return 0;
    case .ok:
        net::Connection connection = made.ok; // keep it where it is until close
        net::CountResult sent = net::write(&connection, request);
        net::CountResult got = sent == .ok ? net::read(&connection, reply) : sent;
        net::close(&connection);
        return got == .ok ? got.ok : 0;
    }
}
```
| Call | Does |
|---|---|
| `connect(host, port)` / `connect_tls(host, port)` / `close(&c)` | a `Connection`: plain TCP (every address tried) / TLS over it, the system's trust deciding with no way to turn it off / releases it |
| `read(&c, buffer)` / `write(&c, data)` | read at least one byte (0 is the end: the peer is done); write all of the bytes (`Reset` when the peer has gone, never SIGPIPE); `read_within(&c, buffer, timeout_ns)` is `read` that gives up with `TimedOut` (on macOS a TLS connection that timed out is cancelled: only `close` is left) |
| `listen(host, port)` / `accept(&l)` / `stop(&l)` | a `Listener` (empty host: every address; port 0: a free one, in `l.port`); the server's `Connection`, blocking; plain TCP only |
- **Misuse is a checked failure**: a second close or stop, a use after it, an empty read buffer, a fake that would wait for ever. On macOS only an expired or not-yet-valid certificate is told apart; any other rejection is `BadCertificate` (OpenSSL names `WrongHost` and `Untrusted`). A macOS TLS connection keeps a block of the current allocator: the region must outlive it.
**The fake, for tests** (nothing is opened):
```smith
module scratch_fake;
import std::arena;
import std::mem;
import std::net;
export Usize greeting_size(arena::Arena *a) {
    arena::ArenaAllocator allocator = arena::allocator(a); // the fake's bytes: the allocator in force
    mem::Allocator *outside = mem::set_allocator(&allocator.base);
    defer mem::set_allocator(outside);
    net::Fake fake = net::fake();
    net::fake_add(&fake, (net::Server){.host = "example.com", .port = 80, .response = "hello"});
    net::Handler *before = net::set_handler(&fake.base);
    defer net::set_handler(before);
    net::ConnectionResult made = net::connect("example.com", 80);
    U8 buffer[:] = mem::alloc_slice(U8, 16);
    switch (made) {
    case .error:
        return 0;
    case .ok:
        net::Connection connection = made.ok;
        net::CountResult got = net::read(&connection, buffer);
        net::close(&connection);
        return got == .ok ? got.ok : 0;
    }
}
```
A `Server` script answers one connect to its host and port (the first not yet used): `response` in pieces of `chunk` bytes and then the end, or `fails` with an `error` (a TLS one stands for a refused certificate), `response_size` for a binary response, `stalls` (stay open and silent: `read_within` times out at once) or `resets` (a read fails with `Reset` after the response); `fake_sent(&fake, connection.id)` is what the program wrote. A fake also listens: `listen` then `connect` to its port pairs the client with what `accept` returns, so a server and its client run in one test (a read with nothing to read and no closed peer is a checked failure). C names are `smith_network_*`; async operations wait for #241. See also [smith-files](../smith-files/SKILL.md), [net](../../../knowledge/net.md).
