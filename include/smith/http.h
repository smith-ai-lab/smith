// <smith/http.h>: an HTTPS client for downloading one file, on <smith/io.h>'s net and files. It is enough to fetch a
// package and nothing more: GET, redirects, the body streamed to a file. No cookies, no authentication, no
// compression, no HTTP/2, no keep-alive (every request is one connection, closed after the response).
//
//   SmithHttpResult result = smith_http_get(&io, "https://example.com/a.tar.gz", "a.tar.gz");
//   if (result.error.code != 0) { smith_console_print_error(&io.console, result.error.message); ... }
//
// The body goes to the file as it arrives, through io.files, a block at a time: a 150 MB download holds about 400 KiB
// in the arena, never the body. A download that fails leaves the file as far as it got, so the caller removes it (or
// writes to a temporary name and renames, as the package cache does).
//
// Redirects (301, 302, 303, 307 and 308) are followed up to ten times, and only to https: a redirect to http:// or any
// other scheme is refused, as is a first URL that is not https:// (or file://, which copies a local file and exists for
// tests). Certificates are the system's (see <smith/io.h>) and cannot be turned off.
//
// A server that sends nothing for smith_http_idle_ns (60 s) fails the download with ETIMEDOUT and a message that names
// the URL and the bytes received so far; the clock is io's, so the fake io tests it by letting its clock pass.
//
// Errors are values with messages that name the URL: a TLS or network failure keeps io's error and code; a status other
// than 2xx is ENOENT for 404 and 410 and EPROTO for the rest ("GET https://host/a: HTTP 404 Not Found"); a redirect
// loop is ELOOP; a redirect to a scheme other than https is EPROTONOSUPPORT; a body shorter than its Content-Length, or
// a connection that ends inside a chunked body, is EIO ("short body: 1000 of 5000 bytes"); a response that is not HTTP
// is EPROTO. A body with no length ends when the server closes the connection, which cannot be checked.
//
// The library is smith's own for now: its only user is smith (`smith_package` and `smith fetch`), so it is not yet
// among the libraries a build.c can ask for with smith_library.
#pragma once

#include <stddef.h>

#include <smith/io.h>

typedef struct {
    SmithError error; // code 0 when the body was written
    int status;       // the status of the last response, 0 when none came (and for file:// URLs)
    size_t size;      // bytes written to the file
    size_t redirects; // redirects followed
} SmithHttpResult;

constexpr uint64_t smith_http_idle_ns = 60'000'000'000;

// Downloads url to the file at path, replacing what is there. The io's arena holds the buffers for the call and is
// given back when it returns.
[[nodiscard]] SmithHttpResult smith_http_get(SmithIo *io, const char *url, const char *path);
