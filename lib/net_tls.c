// The system's TLS stack of std::net on macOS, and the state that loads a TLS stack once (include/smith/net_tls.h). The
// code is C because Network.framework is driven with blocks, which a .smith file cannot write (#2651: C stays C); it
// was std::net's own until then and does what it did. Every system compiles this file; only macOS has the functions.
#include <smith/net_tls.h>

#include <sched.h>
#include <stdatomic.h>

static _Atomic int smith_net_tls_loading;

int smith_net_tls_state_load(void) {
    return atomic_load(&smith_net_tls_loading);
}

bool smith_net_tls_state_claim(void) {
    int expected = 0;
    return atomic_compare_exchange_strong(&smith_net_tls_loading, &expected, 1);
}

void smith_net_tls_state_store(int state) {
    atomic_store(&smith_net_tls_loading, state);
}

#if defined(__APPLE__)

#include <Network/Network.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <errno.h>
#include <os/object.h>
#include <stdio.h>
#include <string.h>

// Network.framework is loaded with dlopen when the first TLS connection is made, never linked: a program that makes no
// connection pays nothing at start, where linking the framework costs about 2.5 ms in every process. The functions and
// the constants the C API names are looked up once. A connection is driven from the calling thread: its callbacks run
// on a serial dispatch queue and signal a semaphore, which the call waits on, so the call blocks.
enum {
    SmithNetTls_Ready = 1,
    SmithNetTls_Failed = 2,
    SmithNetTls_Cancelled = 3,
    SmithNetTls_Tries = 64,
    SmithNetTls_Spins = 4096,
    SmithNetTls_Loaded = 2,
    SmithNetTls_Loading = 1,
    SmithNetTls_Missing = 3,
};

typedef struct {
    typeof(nw_endpoint_create_host) *endpoint_create_host;
    typeof(nw_parameters_create_secure_tcp) *parameters_create_secure_tcp;
    typeof(nw_connection_create) *connection_create;
    typeof(nw_connection_set_queue) *set_queue;
    typeof(nw_connection_set_state_changed_handler) *set_state_handler;
    typeof(nw_connection_start) *start;
    typeof(nw_connection_cancel) *cancel;
    typeof(nw_connection_send) *send;
    typeof(nw_connection_receive) *receive;
    typeof(nw_error_get_error_domain) *error_domain;
    typeof(nw_error_get_error_code) *error_code;
    const nw_parameters_configure_protocol_block_t *configure_default;
    const nw_content_context_t *context_message;
    dispatch_queue_t queue;
} SmithNetTlsApi;

static SmithNetTlsApi smith_net_tls_table;

// What the callbacks of a connection (on the dispatch queue) share with the calls (on the caller's thread): the block
// the caller gave. The semaphores order the writes of one before the reads of the other.
typedef struct {
    nw_connection_t handle;
    dispatch_semaphore_t gate; // the connection's state changes: ready, failed, cancelled
    dispatch_semaphore_t flow; // the send or receive in flight
    int stage;
    int failure;  // the internal code of what the callbacks saw, 0 for none
    size_t taken; // how much the last receive copied
} SmithNetTlsState;

size_t smith_net_tls_state_size(void) {
    return sizeof(SmithNetTlsState);
}

size_t smith_net_tls_state_align(void) {
    return alignof(SmithNetTlsState);
}

// Looks a symbol up and stores its address in the pointer at destination. dlsym returns an address that is data or a
// function; ISO C has no conversion between them, so the address is copied byte for byte.
static bool smith_net_tls_symbol(void *library, const char *name, void *destination) {
    void *address = dlsym(library, name);
    if (address == nullptr) return false;
    memcpy(destination, (const void *)&address, sizeof address);
    return true;
}

static bool smith_net_tls_load_symbols(void *library, SmithNetTlsApi *api) {
    return smith_net_tls_symbol(library, "nw_endpoint_create_host", (void *)&api->endpoint_create_host) &&
           smith_net_tls_symbol(library, "nw_parameters_create_secure_tcp",
                                (void *)&api->parameters_create_secure_tcp) &&
           smith_net_tls_symbol(library, "nw_connection_create", (void *)&api->connection_create) &&
           smith_net_tls_symbol(library, "nw_connection_set_queue", (void *)&api->set_queue) &&
           smith_net_tls_symbol(library, "nw_connection_set_state_changed_handler", (void *)&api->set_state_handler) &&
           smith_net_tls_symbol(library, "nw_connection_start", (void *)&api->start) &&
           smith_net_tls_symbol(library, "nw_connection_cancel", (void *)&api->cancel) &&
           smith_net_tls_symbol(library, "nw_connection_send", (void *)&api->send) &&
           smith_net_tls_symbol(library, "nw_connection_receive", (void *)&api->receive) &&
           smith_net_tls_symbol(library, "nw_error_get_error_domain", (void *)&api->error_domain) &&
           smith_net_tls_symbol(library, "nw_error_get_error_code", (void *)&api->error_code) &&
           smith_net_tls_symbol(library, "_nw_parameters_configure_protocol_default_configuration",
                                (void *)&api->configure_default) &&
           smith_net_tls_symbol(library, "_nw_content_context_default_message", (void *)&api->context_message);
}

static bool smith_net_tls_load(SmithNetTlsApi *api) {
    void *library = dlopen("/System/Library/Frameworks/Network.framework/Network", RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr || !smith_net_tls_load_symbols(library, api)) return false;
    api->queue = dispatch_queue_create("smith.net", nullptr); // a serial queue
    return api->queue != nullptr;
}

// The TLS functions, loaded on the first call by whichever thread asks first (the callbacks run on another thread and
// read the table): null when the system has none.
static const SmithNetTlsApi *smith_net_tls_api(void) {
    if (smith_net_tls_state_load() == 0 && smith_net_tls_state_claim()) {
        bool loaded = smith_net_tls_load(&smith_net_tls_table);
        smith_net_tls_state_store(loaded ? SmithNetTls_Loaded : SmithNetTls_Missing);
    }
    for (int spin = 0; spin < SmithNetTls_Spins && smith_net_tls_state_load() == SmithNetTls_Loading; spin++) {
        sched_yield(); // another thread is loading
    }
    return smith_net_tls_state_load() == SmithNetTls_Loaded ? &smith_net_tls_table : nullptr;
}

// What the system's TLS stack means by a status (Security/SecureTransport.h), for the ones a certificate check gives.
static int smith_net_tls_reason(int status) {
    switch (status) {
    case -9807:
    case -9808: // rejected: the framework does not say whether the host, the chain or the trust was the cause
        return SmithNetworkTls_Invalid;
    case -9812:
    case -9813:
        return SmithNetworkTls_Untrusted;
    case -9814:
        return SmithNetworkTls_Expired;
    case -9815:
        return SmithNetworkTls_NotYet;
    case -9843:
        return SmithNetworkTls_Host;
    default:
        return SmithNetworkTls_Failed;
    }
}

// A Network.framework error as an internal code.
static int smith_net_tls_code(const SmithNetTlsApi *api, nw_error_t error) {
    int code = api->error_code(error);
    switch (api->error_domain(error)) {
    case nw_error_domain_posix:
        return code != 0 ? code : EIO;
    case nw_error_domain_dns:
        return ENOENT;
    case nw_error_domain_tls:
        return SMITH_NET_TLS_BASE + smith_net_tls_reason(code);
    default:
        return EIO;
    }
}

// The state callback: ready or failed ends the connect (a failure cancels the connection first, so the last state
// follows), and the last state (cancelled) ends the close.
static void smith_net_tls_on_state(SmithNetTlsState *state, nw_connection_state_t value, nw_error_t error) {
    const SmithNetTlsApi *api = &smith_net_tls_table;
    if (value == nw_connection_state_ready) {
        state->stage = SmithNetTls_Ready;
        dispatch_semaphore_signal(state->gate);
    } else if (error != nullptr && (value == nw_connection_state_failed || value == nw_connection_state_waiting)) {
        state->failure = smith_net_tls_code(api, error);
        state->stage = SmithNetTls_Failed;
        api->cancel(state->handle);
        dispatch_semaphore_signal(state->gate);
    } else if (value == nw_connection_state_cancelled) {
        state->stage = SmithNetTls_Cancelled;
        dispatch_semaphore_signal(state->gate);
    }
}

void smith_net_tls_close(void **session, void *block) {
    SmithNetTlsState *state = block;
    if (state == nullptr || state->handle == nullptr) return;
    smith_net_tls_table.cancel(state->handle);
    for (int waits = 0; state->stage != SmithNetTls_Cancelled && waits < SmithNetTls_Tries; waits++) {
        dispatch_semaphore_wait(state->gate, DISPATCH_TIME_FOREVER);
    }
    os_release(state->handle);
    dispatch_release(state->gate);
    dispatch_release(state->flow);
    state->handle = nullptr;
    *session = nullptr;
}

int smith_net_tls_connect(void **session, void *block, const char *host, uint16_t port) {
    const SmithNetTlsApi *api = smith_net_tls_api();
    *session = nullptr;
    if (api == nullptr) return ENOSYS;
    SmithNetTlsState *state = block;
    memset(state, 0, sizeof *state);
    state->gate = dispatch_semaphore_create(0);
    state->flow = dispatch_semaphore_create(0);
    char service[8];
    snprintf(service, sizeof service, "%u", (unsigned)port);
    nw_endpoint_t endpoint = api->endpoint_create_host(host, service);
    nw_parameters_t parameters = api->parameters_create_secure_tcp(*api->configure_default, *api->configure_default);
    state->handle = api->connection_create(endpoint, parameters);
    os_release(endpoint);
    os_release(parameters);
    *session = state->handle;
    api->set_queue(state->handle, api->queue);
    api->set_state_handler(state->handle, ^(nw_connection_state_t value, nw_error_t error) {
        smith_net_tls_on_state(state, value, error);
    });
    api->start(state->handle);
    dispatch_semaphore_wait(state->gate, DISPATCH_TIME_FOREVER);
    if (state->failure == 0 && state->stage == SmithNetTls_Ready) return 0;
    int code = state->failure != 0 ? state->failure : EIO;
    smith_net_tls_close(session, block);
    return code;
}

// Copies a received block into the buffer, up to its capacity.
static size_t smith_net_tls_copy(dispatch_data_t content, uint8_t *buffer, size_t capacity) {
    __block size_t copied = 0;
    if (content == nullptr) return 0;
    dispatch_data_apply(content, ^bool(dispatch_data_t region, size_t offset, const void *bytes, size_t size) {
        (void)region;
        (void)offset;
        size_t take = size < capacity - copied ? size : capacity - copied;
        memcpy(buffer + copied, bytes, take);
        copied += take;
        return true;
    });
    return copied;
}

// The receive callback: what arrived, the error or the end.
static void smith_net_tls_on_received(SmithNetTlsState *state, bool *end, dispatch_data_t content, bool complete,
                                      nw_error_t error, uint8_t *buffer, size_t capacity) {
    state->taken = smith_net_tls_copy(content, buffer, capacity);
    if (error != nullptr) {
        state->failure = smith_net_tls_code(&smith_net_tls_table, error);
    } else if (complete) {
        *end = true; // after what this block holds
    }
    dispatch_semaphore_signal(state->flow);
}

ptrdiff_t smith_net_tls_write(void *block, const void *buffer, size_t size, int *code) {
    SmithNetTlsState *state = block;
    const SmithNetTlsApi *api = &smith_net_tls_table;
    state->failure = 0;
    dispatch_data_t data = dispatch_data_create(buffer, size, api->queue, nullptr); // copies the bytes
    api->send(state->handle, data, *api->context_message, true, ^(nw_error_t error) {
        if (error != nullptr) state->failure = smith_net_tls_code(&smith_net_tls_table, error);
        dispatch_semaphore_signal(state->flow);
    });
    dispatch_release(data);
    dispatch_semaphore_wait(state->flow, DISPATCH_TIME_FOREVER);
    if (state->failure != 0) {
        *code = state->failure;
        return -1;
    }
    return (ptrdiff_t)size;
}

// Reads what has arrived (at least one byte), waiting at most timeout_ns (negative: for ever): the count, 0 when the
// peer has finished, or -1 with the code (ETIMEDOUT when nothing came: the connection is cancelled then, since the
// framework cannot take a receive back, and only `close` is left to call).
static ptrdiff_t smith_net_tls_receive(void *block, bool *end, void *buffer, size_t size, int64_t timeout_ns,
                                       int *code) {
    if (*end) return 0;
    SmithNetTlsState *state = block;
    state->taken = 0;
    state->failure = 0;
    smith_net_tls_table.receive(
        state->handle, 1, (uint32_t)size,
        ^(dispatch_data_t content, nw_content_context_t context, bool complete, nw_error_t error) {
            (void)context;
            smith_net_tls_on_received(state, end, content, complete, error, buffer, size);
        });
    dispatch_time_t when = timeout_ns < 0 ? DISPATCH_TIME_FOREVER : dispatch_time(DISPATCH_TIME_NOW, timeout_ns);
    if (dispatch_semaphore_wait(state->flow, when) != 0) {
        smith_net_tls_table.cancel(
            state->handle); // the receive's block runs with an error, and is the last to touch buffer
        dispatch_semaphore_wait(state->flow, DISPATCH_TIME_FOREVER);
        *code = ETIMEDOUT;
        return -1;
    }
    if (state->failure != 0) {
        *code = state->failure;
        return -1;
    }
    return (ptrdiff_t)state->taken;
}

ptrdiff_t smith_net_tls_read(void *block, bool *end, void *buffer, size_t size, int *code) {
    return smith_net_tls_receive(block, end, buffer, size, -1, code);
}

ptrdiff_t smith_net_tls_read_within(void *block, bool *end, void *buffer, size_t size, int64_t timeout_ns, int *code) {
    return smith_net_tls_receive(block, end, buffer, size, timeout_ns, code);
}

bool smith_net_tls_live(const void *session) {
    return session != nullptr;
}

#endif
