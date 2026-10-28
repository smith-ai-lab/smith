---
name: smith-io-net
description: "The network in std::io: io::net_connect, TLS connections, io::Connection and its reads and writes, on Network.framework (macOS) or OpenSSL (Linux). Load when opening a TLS or TCP connection with io::net_connect."
---

# The network

`io.net` makes TLS connections with the platform's stack (Network.framework on macOS, OpenSSL's `libssl.so.3` on Linux, each loaded with `dlopen` on the first connection, so a program that never connects pays nothing at start and a Linux machine without OpenSSL runs everything else). An `io::Connection` is a struct you own and keep in place until its close is done. Everything is an op, with a blocking form:

```smith
import std::io;

void fetch(io::Io *io, const U8 request[:]) {
    io::Connection connection = {};
    io::Error error = io::net_connect(&io->net, &connection, "example.com", 443, true); // tls
    error = io::net_send(&io->net, &connection, request);
    U8 buffer[4096];
    io::Read reply = io::net_receive(&io->net, &connection, buffer); // countof(reply.data) bytes, or reply.end
    (void)reply;
    error = io::net_close(&io->net, &connection);
    (void)error;
}
```

`io::net_connect_async`, `io::net_send_async`, `io::net_receive_async` and `io::net_close_async` take an `io::Op` like the other async calls. One send and one receive may be pending on a connection at once. A receive completes with whatever has arrived (at least one byte, at most `capacity`, not NUL-terminated), or `end` when the peer is done. Close completes what is still pending with `ECANCELED` and is the last thing to touch the connection, so free it only after the close is done; a connection that failed is closed like one that worked.

Failures are values: `ECONNREFUSED` and the other errnos of the system, `ENOENT` for a name that does not resolve, and `EAUTH` for a certificate or handshake failure with the reason in the message ("the certificate has expired"). The system's trust decides and there is no way to turn the check off.

Also in this family: [smith-io](../smith-io/SKILL.md), [smith-io-capabilities](../smith-io-capabilities/SKILL.md), [smith-effects](../smith-effects/SKILL.md), [smith-io-errors](../smith-io-errors/SKILL.md), [smith-io-files](../smith-io-files/SKILL.md), [smith-io-processes](../smith-io-processes/SKILL.md), [smith-io-async](../smith-io-async/SKILL.md), [smith-io-fake](../smith-io-fake/SKILL.md), [smith-io-c](../smith-io-c/SKILL.md).
