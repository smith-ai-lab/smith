// <smith/net_tls.h>: the system's TLS stack of std::net on macOS (Network.framework), and the state that loads a TLS
// stack once on every system. Network.framework is driven with Objective-C blocks, which a .smith file cannot write, so
// that path is C (lib/net_tls.c, #2651: C stays C); std::net calls it through this header. Use `net::connect_tls` from
// a .smith file; there is no C API of its own here.
//
// The codes are std::net's internal ones: 0 for none, an errno, or SMITH_NET_TLS_BASE plus a SmithNetworkTls reason
// (<smith/handlers.h>). Only macOS defines the functions; every system defines the state.
#pragma once

#include <smith/handlers.h>

#include <stddef.h>
#include <stdint.h>

// An internal code at or above this is a TLS reason (SMITH_NET_TLS_BASE + reason), not an errno. std::net's `tls_base`.
#define SMITH_NET_TLS_BASE 1000

// The state of the TLS stack's loading, one for the process: 0 not loaded, 1 loading, 2 loaded, 3 not there. The
// functions are the atomics on it, sequentially consistent. `claim` moves 0 to 1 and says whether this call did, and
// the one that did loads and then stores 2 or 3; the others wait for it to leave 1.
int smith_net_tls_state_load(void);
bool smith_net_tls_state_claim(void);
void smith_net_tls_state_store(int state);

// The size and alignment of the block a connection keeps what its callbacks share with the calls in. It is the
// caller's (std::net takes it from the current allocator, as every allocation of std::net), and it stays valid until
// smith_net_tls_close has returned.
size_t smith_net_tls_state_size(void);
size_t smith_net_tls_state_align(void);

// Connects with TLS to host and port, with state a block of that size and alignment: 0 with *session (the connection)
// set, or the code of the failure with *session null.
int smith_net_tls_connect(void **session, void *state, const char *host, uint16_t port);

// Reads what has arrived (at least one byte) into buffer: the count, 0 when the peer has finished (*end is set once the
// block that carried the end has been read), or -1 with the code in *code.
ptrdiff_t smith_net_tls_read(void *state, bool *end, void *buffer, size_t size, int *code);

// Reads like smith_net_tls_read, giving up with ETIMEDOUT when nothing arrives within timeout_ns. The connection is
// cancelled then, since the framework cannot take a receive back: only smith_net_tls_close is left to call.
ptrdiff_t smith_net_tls_read_within(void *state, bool *end, void *buffer, size_t size, int64_t timeout_ns, int *code);

// Writes all of size bytes: the count, or -1 with the code in *code.
ptrdiff_t smith_net_tls_write(void *state, const void *buffer, size_t size, int *code);

// Cancels the connection, waits for its last state and releases what the connect made; *session is null after it.
void smith_net_tls_close(void **session, void *state);

// Whether the connection made by smith_net_tls_connect is up (its session is not null).
bool smith_net_tls_live(const void *session);
