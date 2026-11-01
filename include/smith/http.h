// <smith/http.h>: an HTTPS client for downloading one file, on <smith/net.h> and <smith/files.h>. It is enough to fetch
// a package and nothing more: GET, redirects, the body streamed to a file. No cookies, no authentication, no
// compression, no HTTP/2, no keep-alive (every request is one connection, closed after the response).
//
//   SmithHttpResult result = smith_http_get("https://example.com/a.tar.gz", "a.tar.gz");
//   if (result.error.code != 0) { fprintf(stderr, "%s\n", result.error.message); ... }
//
// The body goes to the file as it arrives, through std::files, a block at a time: a 150 MB download holds about 400
// KiB from the current allocator (std::mem), never the body. A download that fails leaves the file as far as it got,
// so the caller removes it (or writes to a temporary name and renames, as the package cache does).
//
// Redirects (301, 302, 303, 307 and 308) are followed up to ten times, and only to https: a redirect to http:// or any
// other scheme is refused, as is a first URL that is not https:// (or file://, which copies a local file and exists for
// tests). Certificates are the system's (see <smith/net.h>) and cannot be turned off.
//
// A server that sends nothing for smith_http_idle_ns (60 s) fails the download with ETIMEDOUT and a message that names
// the URL and the bytes received so far; the deadline is std::net's `read_within`, which the fake net answers at
// once, so a test never waits.
//
// Errors are values with messages that name the URL: a TLS or network failure keeps its code (a refused certificate or
// handshake is 80); a status other than 2xx is ENOENT for 404 and 410 and EPROTO for the rest ("GET https://host/a:
// HTTP 404 Not Found"); a redirect loop is ELOOP; a redirect to a scheme other than https is EPROTONOSUPPORT; a body
// shorter than its Content-Length, or a connection that ends inside a chunked body, is EIO ("short body: 1000 of 5000
// bytes"); a response that is not HTTP is EPROTO. A body with no length ends when the server closes the connection,
// which cannot be checked.
//
// The library is smith's own for now: its only user is smith (`smith_package` and `smith fetch`), so it is not yet
// among the libraries a build.c can ask for with smith_library.
#pragma once

#include <stddef.h>

#include <stdint.h>

typedef struct {
    int32_t code; // 0 for success, otherwise an errno value (ENOENT, ETIMEDOUT...)
    char message[192];
} SmithHttpError;

typedef struct {
    SmithHttpError error; // code 0 when the body was written
    int status;           // the status of the last response, 0 when none came (and for file:// URLs)
    size_t size;          // bytes written to the file
    size_t redirects;     // redirects followed
} SmithHttpResult;

constexpr uint64_t smith_http_idle_ns = 60'000'000'000;

// Downloads url to the file at path, replacing what is there. The buffers for the call come from the current allocator
// (std::mem) and are freed when it returns.
[[nodiscard]] SmithHttpResult smith_http_get(const char *url, const char *path);
