---
name: smith-http
description: "smith's HTTPS client, <smith/http.h> and lib/http.c: smith_http_get downloads one URL to a file on io's net and files (redirects to https only, streamed, idle timeout, errors that name the URL), what it does not do, and testing it on the fake io. Load when code calls smith_http_get or includes <smith/http.h>, on a fetch or package download error such as 'short body' or 'HTTP 404', or when asked how smith downloads."
---

# `smith_http_get`: download one file

```c
#include <smith/http.h>

int fetch(SmithIo *io) {
    SmithHttpResult result = smith_http_get(io, "https://example.com/a.tar.gz", "a.tar.gz");
    if (result.error.code != 0) {                        // an error is a value, with a message that names the URL
        (void)smith_console_print_error(&io->console, result.error.message);
        return 1;
    }
    return result.size > 0 ? 0 : 1;                      // result.size bytes were written to the file
}
```

It is the client of `smith fetch` and `smith_package` (`smith-packages`): enough to fetch an archive and nothing more. It is smith's own for now (a C API, no `.smith` module), so a build.c cannot ask for it with `smith_library` yet. It takes the `io` of `main` (`smith-io`), like everything that touches the world.

## What it does

- `smith_http_get(io, url, path)` replaces the file at `path`. The body goes to the file a block at a time through `io.files`: a 150 MB download holds about 400 KiB of the arena, never the body. A failed download leaves the file as far as it got: remove it, or write to a temporary name and rename (as the package cache does).
- `SmithHttpResult` is `error` (code 0 when the body was written), `status` (the last response; 0 when none came, and for `file://`), `size` and `redirects`.
- Redirects (301, 302, 303, 307, 308) are followed up to ten times and only to `https`. The first URL must be `https://` (or `file://`, which copies a local file and exists for tests). Certificates are the system's and cannot be turned off.
- No cookies, authentication, compression, HTTP/2 or keep-alive: every request is one connection.
- A server silent for `smith_http_idle_ns` (60 s) fails with `ETIMEDOUT`, naming the URL and the bytes received.

## Errors

| Message | Code |
| --- | --- |
| `GET https://host/a: HTTP 404 Not Found` | `ENOENT` for 404 and 410, `EPROTO` for other non-2xx |
| a redirect loop | `ELOOP` |
| a redirect to another scheme | `EPROTONOSUPPORT` |
| `short body: 1000 of 5000 bytes`, or a chunked body that ends early | `EIO` |
| a response that is not HTTP | `EPROTO` |
| a TLS or network failure | io's own error and code |

A body with no length ends when the server closes the connection, which cannot be checked.

## Testing

`tests/http_test.smith` runs the same checks on the fake io (files in memory, a scripted network: `io::make_test`, `smith-io-fake`) and on the runner's real io with the same scripted network; the fake's clock passes by `clock_advance`, so the idle timeout needs no wait.
