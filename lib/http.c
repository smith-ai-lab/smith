#include <smith/http.h>

#include <errno.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <smith/files.h>
#include <smith/mem.h>
#include <smith/net.h>

constexpr size_t smith_http_url_max = 2048;
constexpr size_t smith_http_host_max = 256;
constexpr size_t smith_http_head_max = 16384;
constexpr size_t smith_http_read_size = (size_t)128 << 10;
constexpr size_t smith_http_write_size = (size_t)256 << 10;
constexpr size_t smith_http_redirects_max = 10;
constexpr size_t smith_http_rounds_max = (size_t)1 << 32; // receives in one body
constexpr size_t smith_http_shown = 100;                  // characters of a URL in a message
constexpr uint16_t smith_http_default_port = 443;

// A URL taken apart: the host and port to connect to, the text of the authority (host and port as written), and what
// the request asks for (path and query).
typedef struct {
    char host[smith_http_host_max];
    char authority[smith_http_host_max + 8];
    uint16_t port;
    char target[smith_http_url_max];
} SmithHttpUrl;

// Everything one download uses, so that the steps below take one argument.
typedef struct {
    size_t received; // bytes received, head and body
    const char *path;
    char url[smith_http_url_max]; // the URL being fetched now (after redirects)
    SmithHttpUrl parsed;
    char head[smith_http_head_max + 1];
    size_t head_size;
    unsigned char *read_buffer;
    unsigned char *write_buffer;
    size_t write_used;
    SmithHttpResult result;
} SmithHttp;

// What one receive gave: the bytes, the end of the response, or the error.
typedef struct {
    SmithHttpError error;
    size_t count;
    bool end;
} SmithHttpRead;

[[gnu::format(printf, 3, 4)]] static void smith_http_fail(SmithHttp *http, int code, const char *format, ...) {
    SmithHttpError error = {.code = code};
    va_list arguments;
    va_start(arguments, format);
    [[maybe_unused]] int written = vsnprintf(error.message, sizeof error.message, format, arguments);
    va_end(arguments);
    if (written <= 0) __builtin_trap(); // the formats here are fixed and fit the message
    http->result.error = error;
}

// ---------------------------------------------------------------- URLs

static bool smith_http_starts(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool smith_http_digits(const char *text, size_t length) {
    if (length == 0 || length > 5) return false;
    for (size_t i = 0; i < length; i++)
        if (text[i] < '0' || text[i] > '9') return false;
    return true;
}

// Splits "https://host[:port]/target" into the parts; false with a reason for anything else.
static bool smith_http_parse(const char *url, SmithHttpUrl *parsed, const char **reason) {
    *parsed = (SmithHttpUrl){.port = smith_http_default_port};
    size_t length = strlen(url);
    if (length >= smith_http_url_max) {
        *reason = "the URL is too long";
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if ((unsigned char)url[i] <= ' ' || (unsigned char)url[i] >= 0x7f) {
            *reason = "the URL has a space or a control character";
            return false;
        }
    }
    if (!smith_http_starts(url, "https://")) {
        *reason = "only https URLs are fetched";
        return false;
    }
    const char *authority = url + strlen("https://");
    size_t span = strcspn(authority, "/?#");
    if (span == 0 || span >= sizeof parsed->host || memchr(authority, '@', span) != nullptr) {
        *reason = "the URL has no host, has a user name, or its host is too long";
        return false;
    }
    memcpy(parsed->authority, authority, span);
    const char *colon = memchr(authority, ':', span);
    const char *bracket = memchr(authority, ']', span);
    if (colon != nullptr && bracket != nullptr && colon < bracket) colon = nullptr; // an IPv6 address's colons
    size_t host_length = colon != nullptr ? (size_t)(colon - authority) : span;
    if (colon != nullptr) {
        if (!smith_http_digits(colon + 1, span - host_length - 1)) {
            *reason = "the URL's port is not a number";
            return false;
        }
        parsed->port = (uint16_t)strtoul(colon + 1, nullptr, 10);
    }
    if (authority[0] == '[' && host_length >= 2) {
        memcpy(parsed->host, authority + 1, host_length - 2);
    } else {
        memcpy(parsed->host, authority, host_length);
    }
    const char *rest = authority + span;
    size_t target_length = strcspn(rest, "#");
    if (target_length == 0 || rest[0] == '?')
        snprintf(parsed->target, sizeof parsed->target, "/%.*s", (int)target_length, rest);
    else
        snprintf(parsed->target, sizeof parsed->target, "%.*s", (int)target_length, rest);
    return parsed->host[0] != 0;
}

// The URL a Location header names, relative to the one that was fetched, written to out.
static bool smith_http_resolve(const SmithHttpUrl *base, const char *location, char *out, size_t capacity) {
    int written = 0;
    if (strstr(location, "://") != nullptr) {
        written = snprintf(out, capacity, "%s", location);
    } else if (smith_http_starts(location, "//")) {
        written = snprintf(out, capacity, "https:%s", location);
    } else if (location[0] == '/') {
        written = snprintf(out, capacity, "https://%s%s", base->authority, location);
    } else {
        size_t path_length = strcspn(base->target, "?");
        while (path_length > 0 && base->target[path_length - 1] != '/')
            path_length -= 1;
        written =
            snprintf(out, capacity, "https://%s%.*s%s", base->authority, (int)path_length, base->target, location);
    }
    return written > 0 && (size_t)written < capacity;
}

// ---------------------------------------------------------------- the world's errors

static struct smith_array_const_char_n smith_http_text(const char *text) {
    return (struct smith_array_const_char_n){.data = text, .count = {strlen(text)}};
}

// The errno of a failed file call.
static int smith_http_files_code(SmithFilesError error) {
    switch (error) {
    case SmithFilesError_NotFound:
        return ENOENT;
    case SmithFilesError_Exists:
        return EEXIST;
    case SmithFilesError_Denied:
        return EACCES;
    case SmithFilesError_IsFolder:
        return EISDIR;
    case SmithFilesError_NotFolder:
        return ENOTDIR;
    case SmithFilesError_NotEmpty:
        return ENOTEMPTY;
    case SmithFilesError_NameTooLong:
        return ENAMETOOLONG;
    case SmithFilesError_NoSpace:
        return ENOSPC;
    case SmithFilesError_ReadOnly:
        return EROFS;
    case SmithFilesError_TooManyOpen:
        return EMFILE;
    case SmithFilesError_Other:
        return EIO;
    }
    return EIO;
}

// A failed file call as the result's error: the errno and its text.
static SmithHttpError smith_http_files_error(SmithFilesError error) {
    SmithHttpError result = {.code = smith_http_files_code(error)};
    snprintf(result.message, sizeof result.message, "%s", strerror(result.code));
    return result;
}

constexpr int smith_http_tls_code = 80; // EAUTH on macOS, the code of a refused certificate or handshake on both systems

// The errno a failed network call stands for, and what to say about it.
static int smith_http_net_code(SmithNetworkError error, const char **reason) {
    switch (error) {
    case SmithNetworkError_NotFound:
        *reason = "the host name does not resolve";
        return ENOENT;
    case SmithNetworkError_Refused:
        *reason = "connection refused";
        return ECONNREFUSED;
    case SmithNetworkError_Unreachable:
        *reason = "no route to the host";
        return EHOSTUNREACH;
    case SmithNetworkError_TimedOut:
        *reason = "timed out";
        return ETIMEDOUT;
    case SmithNetworkError_Reset:
        *reason = "connection reset";
        return ECONNRESET;
    case SmithNetworkError_Expired:
        *reason = "the certificate has expired";
        return smith_http_tls_code;
    case SmithNetworkError_NotYetValid:
        *reason = "the certificate is not valid yet";
        return smith_http_tls_code;
    case SmithNetworkError_WrongHost:
        *reason = "the certificate is not for this host";
        return smith_http_tls_code;
    case SmithNetworkError_Untrusted:
        *reason = "the certificate is not trusted";
        return smith_http_tls_code;
    case SmithNetworkError_BadCertificate:
        *reason = "the certificate was rejected";
        return smith_http_tls_code;
    case SmithNetworkError_HandshakeFailed:
        *reason = "the TLS handshake failed";
        return smith_http_tls_code;
    case SmithNetworkError_Unsupported:
        *reason = "TLS is not available on this system";
        return ENOSYS;
    case SmithNetworkError_InUse:
    case SmithNetworkError_Denied:
    case SmithNetworkError_TooMany:
    case SmithNetworkError_Other:
        break;
    }
    *reason = "the network call failed";
    return EIO;
}

// A failed network call as the result's error, naming the URL being fetched.
static SmithHttpError smith_http_net_error(const SmithHttp *http, SmithNetworkError error) {
    const char *reason = "";
    SmithHttpError result = {.code = smith_http_net_code(error, &reason)};
    snprintf(result.message, sizeof result.message, "GET %.*s: %s", (int)smith_http_shown, http->url, reason);
    return result;
}

// ---------------------------------------------------------------- the file

// Writes what is buffered to the file. Errors stay in the result and stop later writes.
static void smith_http_flush(SmithHttp *http) {
    if (http->write_used == 0 || http->result.error.code != 0) return;
    smith_files_CountResult written = smith_fs_write_file(
        smith_http_text(http->path),
        (struct smith_array_const_u8_n){.data = (const uint8_t *)(http->write_buffer), .count = {http->write_used}},
        SmithFilesMode_Append);
    if (written.tag == smith_enum_result_usize__SmithFilesError_tag_error)
        http->result.error = smith_http_files_error(written.error);
    http->write_used = 0;
}

// Adds body bytes to the file, a block at a time.
static void smith_http_put(SmithHttp *http, const unsigned char *data, size_t size) {
    http->result.size += size;
    for (size_t at = 0; at < size && http->result.error.code == 0;) {
        size_t take = smith_http_write_size - http->write_used;
        if (take > size - at) take = size - at;
        memcpy(http->write_buffer + http->write_used, data + at, take);
        http->write_used += take;
        at += take;
        if (http->write_used == smith_http_write_size) smith_http_flush(http);
    }
}

// ---------------------------------------------------------------- headers

static char smith_http_lower(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

// The value of a header of the response (case does not matter), copied to out; false if it is not there.
static bool smith_http_header(const SmithHttp *http, const char *name, char *out, size_t capacity) {
    size_t length = strlen(name);
    const char *line = strstr(http->head, "\r\n");
    for (size_t lines = 0; line != nullptr && lines < smith_http_head_max; lines++) {
        line += 2;
        bool same = line[length] == ':';
        for (size_t i = 0; same && i < length; i++)
            same = smith_http_lower(line[i]) == name[i];
        if (same) {
            const char *value = line + length + 1;
            value += strspn(value, " \t");
            size_t span = strcspn(value, "\r\n");
            if (span >= capacity) return false;
            memcpy(out, value, span);
            out[span] = 0;
            return true;
        }
        line = strstr(line, "\r\n");
    }
    return false;
}

// ---------------------------------------------------------------- receiving

// One receive, given up on when nothing arrives for smith_http_idle_ns: the result is ETIMEDOUT with the URL and the
// bytes that did arrive.
static SmithHttpRead smith_http_receive(SmithHttp *http, SmithNetworkConnection *connection, void *buffer,
                                        size_t capacity) {
    smith_net_CountResult received = smith_network_read_within(
        connection, (struct smith_array_u8_n){.data = (uint8_t *)(buffer), .count = {capacity}},
        (int64_t)smith_http_idle_ns);
    SmithHttpRead read = {};
    if (received.tag == smith_enum_result_usize__SmithNetworkError_tag_ok) {
        read.count = received.ok;
        read.end = received.ok == 0;
        http->received += received.ok;
        return read;
    }
    if (received.tag == smith_enum_result_usize__SmithNetworkError_tag_error &&
        received.error != SmithNetworkError_TimedOut) {
        read.error = smith_http_net_error(http, received.error);
        return read;
    }
    read.error.code = ETIMEDOUT;
    snprintf(read.error.message, sizeof read.error.message,
             "GET %.*s: timed out: nothing received for %llu s, after %zu bytes", (int)smith_http_shown, http->url,
             (unsigned long long)(smith_http_idle_ns / 1'000'000'000), http->received);
    return read;
}

// ---------------------------------------------------------------- the body

typedef enum {
    SmithBody_Length, // Content-Length bytes
    SmithBody_Chunked,
    SmithBody_ToEnd, // until the server closes the connection
} SmithBodyMode;

typedef enum {
    SmithChunk_Size,
    SmithChunk_Extension,
    SmithChunk_SizeEnd,
    SmithChunk_Data,
    SmithChunk_DataCr,
    SmithChunk_DataLf,
    SmithChunk_TrailerStart,
    SmithChunk_TrailerLine,
    SmithChunk_TrailerEnd,
} SmithChunk;

typedef struct {
    SmithBodyMode mode;
    size_t length; // Length: the bytes the body has; Chunked: the bytes left in this chunk
    size_t got;    // Length: the bytes so far
    SmithChunk chunk;
    bool digits; // Chunked: a size digit has been read
    bool done;
    bool malformed;
} SmithBody;

static int smith_http_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// One byte of the chunked framing outside the data: the size line, the line ends and the trailers.
static void smith_http_chunk_byte(SmithBody *body, char c) {
    switch (body->chunk) {
    case SmithChunk_Size: {
        int digit = smith_http_hex(c);
        if (digit >= 0 && body->length <= (SIZE_MAX >> 4)) {
            body->length = body->length * 16 + (size_t)digit;
            body->digits = true;
        } else if (body->digits && c == ';') {
            body->chunk = SmithChunk_Extension;
        } else if (body->digits && c == '\r') {
            body->chunk = SmithChunk_SizeEnd;
        } else {
            body->malformed = true;
        }
        break;
    }
    case SmithChunk_Extension:
        if (c == '\r') body->chunk = SmithChunk_SizeEnd;
        break;
    case SmithChunk_SizeEnd:
        body->malformed = c != '\n';
        body->chunk = body->length == 0 ? SmithChunk_TrailerStart : SmithChunk_Data;
        break;
    case SmithChunk_DataCr:
        body->malformed = c != '\r';
        body->chunk = SmithChunk_DataLf;
        break;
    case SmithChunk_DataLf:
        body->malformed = c != '\n';
        body->chunk = SmithChunk_Size;
        body->digits = false;
        break;
    case SmithChunk_TrailerStart:
        body->chunk = c == '\r' ? SmithChunk_TrailerEnd : SmithChunk_TrailerLine;
        break;
    case SmithChunk_TrailerLine:
        if (c == '\n') body->chunk = SmithChunk_TrailerStart;
        break;
    case SmithChunk_TrailerEnd:
        body->malformed = c != '\n';
        body->done = true;
        break;
    case SmithChunk_Data:
        break;
    }
}

// Feeds received bytes to a chunked body, writing the chunks' data to the file.
static void smith_http_feed_chunked(SmithHttp *http, SmithBody *body, const unsigned char *data, size_t size) {
    for (size_t at = 0; at < size && !body->done && !body->malformed;) {
        if (body->chunk != SmithChunk_Data) {
            smith_http_chunk_byte(body, (char)data[at]);
            at += 1;
            continue;
        }
        size_t take = size - at < body->length ? size - at : body->length;
        smith_http_put(http, data + at, take);
        body->length -= take;
        at += take;
        if (body->length == 0) body->chunk = SmithChunk_DataCr;
    }
}

static void smith_http_feed(SmithHttp *http, SmithBody *body, const unsigned char *data, size_t size) {
    if (body->mode == SmithBody_Chunked) {
        smith_http_feed_chunked(http, body, data, size);
    } else if (body->mode == SmithBody_Length) {
        size_t take = size < body->length - body->got ? size : body->length - body->got;
        smith_http_put(http, data, take);
        body->got += take;
        body->done = body->got == body->length;
    } else {
        smith_http_put(http, data, size);
    }
}

// The body's mode from the response's headers; false (with the error set) for a Content-Length that is no number.
static bool smith_http_body_mode(SmithHttp *http, SmithBody *body) {
    char value[64];
    *body = (SmithBody){.mode = SmithBody_ToEnd};
    if (smith_http_header(http, "transfer-encoding", value, sizeof value) && strstr(value, "chunked") != nullptr) {
        body->mode = SmithBody_Chunked;
        return true;
    }
    if (!smith_http_header(http, "content-length", value, sizeof value)) return true;
    char *end = nullptr;
    errno = 0;
    unsigned long long length = strtoull(value, &end, 10);
    if (value[0] < '0' || value[0] > '9' || *end != 0 || errno != 0) {
        smith_http_fail(http, EPROTO, "GET %.*s: the Content-Length \"%.20s\" is not a number", (int)smith_http_shown,
                        http->url, value);
        return false;
    }
    body->mode = SmithBody_Length;
    body->length = (size_t)length;
    body->done = length == 0;
    return true;
}

// Reads the rest of the body from the connection until the framing says it is done, or the server closes it.
static void smith_http_read_body(SmithHttp *http, SmithNetworkConnection *connection, SmithBody *body, size_t already) {
    smith_http_feed(http, body, http->read_buffer, already);
    for (size_t round = 0;
         round < smith_http_rounds_max && !body->done && !body->malformed && http->result.error.code == 0; round++) {
        SmithHttpRead read = smith_http_receive(http, connection, http->read_buffer, smith_http_read_size);
        if (read.error.code != 0) {
            smith_http_flush(http); // the file keeps the body that arrived
            http->result.error = read.error;
            return;
        }
        if (read.end) break;
        smith_http_feed(http, body, http->read_buffer, read.count);
    }
    smith_http_flush(http);
    if (http->result.error.code != 0 || body->done || body->mode == SmithBody_ToEnd) return;
    if (body->malformed) {
        smith_http_fail(http, EPROTO, "GET %.*s: the chunked body is malformed", (int)smith_http_shown, http->url);
    } else if (body->mode == SmithBody_Length) {
        smith_http_fail(http, EIO, "GET %.*s: short body: %zu of %zu bytes", (int)smith_http_shown, http->url,
                        body->got, body->length);
    } else {
        smith_http_fail(http, EIO, "GET %.*s: short body: the connection ended inside a chunked body, after %zu bytes",
                        (int)smith_http_shown, http->url, http->result.size);
    }
}

// ---------------------------------------------------------------- one request

static void smith_http_request(SmithHttp *http, SmithNetworkConnection *connection) {
    char request[smith_http_url_max + 512];
    int written = snprintf(request, sizeof request,
                           "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: smith\r\nAccept: */*\r\n"
                           "Accept-Encoding: identity\r\nConnection: close\r\n\r\n",
                           http->parsed.target, http->parsed.authority);
    if (written <= 0 || (size_t)written >= sizeof request) __builtin_trap(); // the url and host are capped to fit
    smith_net_CountResult sent = smith_network_write(
        connection,
        (struct smith_array_const_u8_n){.data = (const uint8_t *)(request), .count = {(size_t)written}});
    if (sent.tag == smith_enum_result_usize__SmithNetworkError_tag_error)
        http->result.error = smith_http_net_error(http, sent.error);
}

// Reads until the end of the response head (a blank line). The bytes after it stay in the buffer: the return is how
// many. SIZE_MAX means the head did not come, with the error set.
static size_t smith_http_read_head(SmithHttp *http, SmithNetworkConnection *connection) {
    http->head_size = 0;
    for (size_t round = 0; round < smith_http_head_max; round++) {
        SmithHttpRead read =
            smith_http_receive(http, connection, http->head + http->head_size, smith_http_head_max - http->head_size);
        if (read.error.code != 0) {
            http->result.error = read.error;
            return SIZE_MAX;
        }
        if (read.end) {
            smith_http_fail(http, EPROTO, "GET %.*s: the connection closed before the response head ended",
                            (int)smith_http_shown, http->url);
            return SIZE_MAX;
        }
        size_t from = http->head_size > 3 ? http->head_size - 3 : 0;
        http->head_size += read.count;
        http->head[http->head_size] = 0;
        const char *blank = strstr(http->head + from, "\r\n\r\n");
        if (blank != nullptr) { // the body bytes that came with the head move to the read buffer
            size_t head_length = (size_t)(blank - http->head) + 4;
            size_t after = http->head_size - head_length;
            memcpy(http->read_buffer, http->head + head_length, after);
            http->head[head_length] = 0;
            return after;
        }
        if (http->head_size == smith_http_head_max) break;
    }
    smith_http_fail(http, EPROTO, "GET %.*s: the response head is longer than %zu bytes", (int)smith_http_shown,
                    http->url, smith_http_head_max);
    return SIZE_MAX;
}

// What the head says: the status, or false with the error set when it is not HTTP.
static bool smith_http_status(SmithHttp *http) {
    const char *at = http->head + strlen("HTTP/1.x ");
    bool http_one = smith_http_starts(http->head, "HTTP/1.") && http->head[strlen("HTTP/1.x")] == ' ';
    bool digits =
        http_one && at[0] >= '1' && at[0] <= '5' && at[1] >= '0' && at[1] <= '9' && at[2] >= '0' && at[2] <= '9';
    if (!digits) {
        smith_http_fail(http, EPROTO, "GET %.*s: the reply is not HTTP", (int)smith_http_shown, http->url);
        return false;
    }
    http->result.status = (at[0] - '0') * 100 + (at[1] - '0') * 10 + (at[2] - '0');
    return true;
}

static void smith_http_status_error(SmithHttp *http) {
    char reason[48] = "";
    const char *space = strchr(http->head + strlen("HTTP/1.x "), ' ');
    if (space != nullptr) snprintf(reason, sizeof reason, "%.*s", (int)strcspn(space + 1, "\r\n"), space + 1);
    int status = http->result.status;
    smith_http_fail(http, status == 404 || status == 410 ? ENOENT : EPROTO, "GET %.*s: HTTP %d%s%s",
                    (int)smith_http_shown, http->url, status, reason[0] != 0 ? " " : "", reason);
}

typedef enum {
    SmithHop_Done,
    SmithHop_Redirect,
    SmithHop_Failed,
} SmithHop;

// A redirect's new URL goes to http->url; false with the error set when it is refused.
static bool smith_http_follow(SmithHttp *http) {
    char location[smith_http_url_max];
    if (!smith_http_header(http, "location", location, sizeof location)) {
        smith_http_fail(http, EPROTO, "GET %.*s: HTTP %d with no Location", (int)smith_http_shown, http->url,
                        http->result.status);
        return false;
    }
    if (http->result.redirects == smith_http_redirects_max) {
        smith_http_fail(http, ELOOP, "GET %.*s: more than %zu redirects", (int)smith_http_shown, http->url,
                        smith_http_redirects_max);
        return false;
    }
    char next[smith_http_url_max];
    const char *reason = "the Location is too long";
    if (!smith_http_resolve(&http->parsed, location, next, sizeof next) ||
        !smith_http_parse(next, &http->parsed, &reason)) {
        smith_http_fail(http, EPROTONOSUPPORT, "GET %.*s: redirect to %.*s refused: %s", (int)smith_http_shown,
                        http->url, (int)smith_http_shown, location, reason);
        return false;
    }
    http->result.redirects += 1;
    snprintf(http->url, sizeof http->url, "%s", next);
    return true;
}

// One request on a connected connection: the response is a redirect to follow, the body written, or a failure.
static SmithHop smith_http_exchange(SmithHttp *http, SmithNetworkConnection *connection) {
    SmithHop hop = SmithHop_Failed;
    smith_http_request(http, connection);
    size_t already = http->result.error.code == 0 ? smith_http_read_head(http, connection) : SIZE_MAX;
    SmithBody body = {};
    if (already != SIZE_MAX && smith_http_status(http)) {
        int status = http->result.status;
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            if (smith_http_follow(http)) hop = SmithHop_Redirect;
        } else if (status < 200 || status > 299) {
            smith_http_status_error(http);
        } else if (smith_http_body_mode(http, &body)) {
            smith_http_read_body(http, connection, &body, already);
            if (http->result.error.code == 0) hop = SmithHop_Done;
        }
    }
    return hop;
}

// One request on one connection.
static SmithHop smith_http_hop(SmithHttp *http) {
    SmithHop hop = SmithHop_Failed;
    smith_net_ConnectionResult connected =
        smith_network_connect_tls(smith_http_text(http->parsed.host), http->parsed.port);
    if (connected.tag == smith_enum_result_SmithNetworkConnection__SmithNetworkError_tag_ok) {
        SmithNetworkConnection connection = connected.ok;
        hop = smith_http_exchange(http, &connection);
        smith_network_close(&connection);
    }
    if (connected.tag == smith_enum_result_SmithNetworkConnection__SmithNetworkError_tag_error)
        http->result.error = smith_http_net_error(http, connected.error);
    return hop;
}

// ---------------------------------------------------------------- local files

// file:///path: the file's bytes to the destination, in blocks, as a download would arrive.
static void smith_http_copy_local(SmithHttp *http, const char *source) {
    smith_files_InfoResult info = smith_fs_info(smith_http_text(source));
    if (info.tag == smith_enum_result_SmithFilesInfo__SmithFilesError_tag_error) {
        int code = smith_http_files_code(info.error);
        smith_http_fail(http, code, "GET %.*s: %s", (int)smith_http_shown, http->url, strerror(code));
        return;
    }
    if (info.tag == smith_enum_result_SmithFilesInfo__SmithFilesError_tag_ok && info.ok.folder) {
        smith_http_fail(http, EISDIR, "GET %.*s: %s", (int)smith_http_shown, http->url, strerror(EISDIR));
        return;
    }
    if (info.tag == smith_enum_result_SmithFilesInfo__SmithFilesError_tag_ok && info.ok.size == 0) return;
    smith_files_BytesResult mapped = smith_fs_map(smith_http_text(source));
    if (mapped.tag == smith_enum_result_smith_files_Bytes__SmithFilesError_tag_error) {
        http->result.error = smith_http_files_error(mapped.error);
    }
    if (mapped.tag == smith_enum_result_smith_files_Bytes__SmithFilesError_tag_ok) {
        smith_http_put(http, mapped.ok.data, mapped.ok.count[0]);
        smith_http_flush(http);
        smith_fs_unmap(mapped.ok);
    }
}

// ---------------------------------------------------------------- the call

static void smith_http_run(SmithHttp *http, const char *url) {
    snprintf(http->url, sizeof http->url, "%s", url);
    if (smith_http_starts(url, "file://")) {
        smith_http_copy_local(http, url + strlen("file://"));
        return;
    }
    for (size_t hops = 0; hops <= smith_http_redirects_max; hops++) {
        const char *reason = "";
        if (!smith_http_parse(http->url, &http->parsed, &reason)) {
            smith_http_fail(http, hops == 0 ? EINVAL : EPROTONOSUPPORT, "GET %.*s: %s", (int)smith_http_shown,
                            http->url, reason);
            return;
        }
        if (smith_http_hop(http) != SmithHop_Redirect) return;
    }
}

SmithHttpResult smith_http_get(const char *url, const char *path) {
    SmithHttp *http = smith_mem_alloc(sizeof(SmithHttp), alignof(SmithHttp));
    http->path = path;
    http->read_buffer = smith_mem_alloc(smith_http_read_size, 1);
    http->write_buffer = smith_mem_alloc(smith_http_write_size, 1);
    smith_files_CountResult created = smith_fs_write_file(
        smith_http_text(path), (struct smith_array_const_u8_n){}, SmithFilesMode_Replace); // an empty file, or none
    if (created.tag == smith_enum_result_usize__SmithFilesError_tag_error)
        http->result.error = smith_http_files_error(created.error);
    if (http->result.error.code == 0) smith_http_run(http, url);
    SmithHttpResult result = http->result;
    smith_mem_free(http->write_buffer);
    smith_mem_free(http->read_buffer);
    smith_mem_free(http);
    return result;
}
