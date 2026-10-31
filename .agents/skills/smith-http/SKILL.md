---
name: smith-http
description: "smith's HTTPS client, <smith/http.h> and lib/http.c: smith_http_get downloads one URL to a file on smith::net and smith::files (redirects to https only, streamed, idle timeout, errors that name the URL), what it does not do, and testing it on the net and files fakes. Load when code calls smith_http_get or includes <smith/http.h>, on a fetch or package download error such as 'short body' or 'HTTP 404', or when asked how smith downloads."
---

# `smith_http_get`: download one file

```c
#include <smith/http.h>

int fetch(void) {
    SmithHttpResult result = smith_http_get("https://example.com/a.tar.gz", "a.tar.gz");
    if (result.error.code != 0) {                        // an error is a value, with a message that names the URL
        fprintf(stderr, "%s\n", result.error.message);
        return 1;
    }
    return result.size > 0 ? 0 : 1;                      // result.size bytes were written to the file
}
```

It is the client of `smith fetch` and `smith_package` (`smith-packages`): enough to fetch an archive and nothing more. It is smith's own for now (a C API, no `.smith` module), so a build.c cannot ask for it with `smith_library` yet. It takes no io: it calls `net` (`smith-net`) and `files` (`smith-files`) directly, so a thread's net and files handlers (the fakes) decide what it touches, and its buffers come from the current allocator.

## What it does

- `smith_http_get(url, path)` replaces the file at `path`. The body goes to the file a block at a time through `smith::files`: a 150 MB download holds about 400 KiB of the current allocator's memory, never the body. A failed download leaves the file as far as it got: remove it, or write to a temporary name and rename (as the package cache does).
- `SmithHttpResult` is `error` (code 0 when the body was written), `status` (the last response; 0 when none came, and for `file://`), `size` and `redirects`.
- Redirects (301, 302, 303, 307, 308) are followed up to ten times and only to `https`. The first URL must be `https://` (or `file://`, which copies a local file and exists for tests). Certificates are the system's and cannot be turned off.
- No cookies, authentication, compression, HTTP/2 or keep-alive: every request is one connection.
- A server silent for `smith_http_idle_ns` (60 s) fails with `ETIMEDOUT`, naming the URL and the bytes received: each read is `net::read_within` with that deadline.

## Errors

| Message | Code |
| --- | --- |
| `GET https://host/a: HTTP 404 Not Found` | `ENOENT` for 404 and 410, `EPROTO` for other non-2xx |
| a redirect loop | `ELOOP` |
| a redirect to another scheme | `EPROTONOSUPPORT` |
| `short body: 1000 of 5000 bytes`, or a chunked body that ends early | `EIO` |
| a response that is not HTTP | `EPROTO` |
| a TLS or network failure | the net error as an errno (`ECONNREFUSED`, `ECONNRESET`...; 80 for a refused certificate or handshake), with the reason in the message |

A body with no length ends when the server closes the connection, which cannot be checked.

## Testing

`lib/http_test.smith` takes no io: it sets `net::fake()` (a scripted network, `net::fake_add`, `net::fake_sent` for the request) and `files::fake()` as the thread's handlers. A `Server` with `stalls` stays open and silent, and the fake answers `read_within` with nothing to give at once, so the idle timeout needs no wait.
