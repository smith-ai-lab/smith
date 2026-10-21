// Tests of <smith/http.h> on a scripted network: the same checks run on the fake io (files in memory) and on a real io
// with a scripted network (files on the disk, in the folder the test runs in). Nothing here touches the internet.

#include <smith/http.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    alignas(smith_arena_align_max) unsigned char bytes[8 << 20];
} TestMemory;

static bool contains(const char *text, const char *needle) {
    return strstr(text, needle) != nullptr;
}

static void serve(SmithIo *io, const char *host, const char *response) {
    SmithNetServer server = {.host = host, .port = 443, .response = response, .response_size = strlen(response)};
    assert(smith_io_test_add_server(io, server).code == 0);
}

static void serve_chunks(SmithIo *io, const char *host, const char *response, size_t chunk) {
    SmithNetServer server = {
        .host = host, .port = 443, .response = response, .response_size = strlen(response), .chunk = chunk};
    assert(smith_io_test_add_server(io, server).code == 0);
}

static SmithHttpResult get(SmithIo *io, const char *url) {
    return smith_http_get(&io->net, &io->files, &io->arena, url, "download.bin");
}

static const char *body_of(SmithIo *io) {
    SmithRead read = smith_files_read(&io->files, &io->arena, "download.bin");
    assert(read.error.code == 0);
    return read.data;
}

static void check_basics(SmithIo *io) {
    serve(io, "a.example", "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Type: text/plain\r\n\r\nhello");
    SmithHttpResult result = get(io, "https://a.example/dir/file.txt?x=1#fragment");
    assert(result.error.code == 0 && result.status == 200 && result.size == 5 && result.redirects == 0);
    assert(strcmp(body_of(io), "hello") == 0);
    const char *request = smith_io_test_request(io, "a.example", 443, 0);
    assert(request != nullptr && strncmp(request, "GET /dir/file.txt?x=1 HTTP/1.1\r\nHost: a.example\r\n", 48) == 0);
    assert(contains(request, "Connection: close\r\n") && contains(request, "User-Agent: smith\r\n"));

    // Header names in any case, and the head split into pieces of three bytes.
    serve_chunks(io, "b.example", "HTTP/1.0 200 OK\r\ncontent-LENGTH:  6\r\n\r\nsix!!!", 3);
    result = get(io, "https://b.example");
    assert(result.error.code == 0 && result.size == 6 && strcmp(body_of(io), "six!!!") == 0);
    assert(strncmp(smith_io_test_request(io, "b.example", 443, 0), "GET / HTTP/1.1\r\nHost: b.example\r\n", 33) == 0);

    // No length: the body ends when the server closes the connection. An empty body is an empty file.
    serve(io, "c.example", "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nuntil the end");
    result = get(io, "https://c.example/c");
    assert(result.error.code == 0 && result.size == 13 && strcmp(body_of(io), "until the end") == 0);
    serve(io, "d.example", "HTTP/1.1 204 No Content\r\n\r\n");
    result = get(io, "https://d.example/d");
    assert(result.error.code == 0 && result.size == 0 && strcmp(body_of(io), "") == 0);

    // A port other than 443 is in the Host header.
    const char *response = "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\ne";
    SmithNetServer server = {
        .host = "e.example", .port = 8443, .response = response, .response_size = strlen(response)};
    assert(smith_io_test_add_server(io, server).code == 0);
    result = get(io, "https://e.example:8443/e");
    assert(result.error.code == 0 && strcmp(body_of(io), "e") == 0);
    assert(contains(smith_io_test_request(io, "e.example", 8443, 0), "Host: e.example:8443\r\n"));
}

static void check_chunked(SmithIo *io) {
    const char *response = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                           "5\r\nhello\r\n7;ext=1\r\n, world\r\nA\r\n0123456789\r\n0\r\nTrailer: x\r\n\r\n";
    serve(io, "f.example", response);
    SmithHttpResult result = get(io, "https://f.example/f");
    assert(result.error.code == 0 && result.size == 22 && strcmp(body_of(io), "hello, world0123456789") == 0);
    for (size_t chunk = 1; chunk <= 5; chunk++) { // the framing split anywhere
        serve_chunks(io, "g.example", response, chunk);
        result = get(io, "https://g.example/g");
        assert(result.error.code == 0 && strcmp(body_of(io), "hello, world0123456789") == 0);
    }
    serve(io, "h.example", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhel");
    result = get(io, "https://h.example/h");
    assert(result.error.code == EIO && contains(result.error.message, "short body"));
    assert(contains(result.error.message, "inside a chunked body"));
    serve(io, "i.example", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\nhello\r\n");
    result = get(io, "https://i.example/i");
    assert(result.error.code == EPROTO && contains(result.error.message, "malformed"));
}

static void check_redirects(SmithIo *io) {
    serve(io, "r1.example",
          "HTTP/1.1 302 Found\r\nLocation: https://r2.example/moved/file?q=2\r\nContent-Length: 0\r\n\r\n");
    serve(io, "r2.example", "HTTP/1.1 301 Moved\r\nlocation: /moved/dir/other\r\n\r\n");
    serve(io, "r2.example", "HTTP/1.1 307 Moved\r\nLocation: sibling.txt\r\n\r\n");
    serve(io, "r2.example", "HTTP/1.1 308 Moved\r\nLocation: //r3.example/last\r\n\r\n");
    serve(io, "r3.example", "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\ndone");
    SmithHttpResult result = get(io, "https://r1.example/start");
    assert(result.error.code == 0 && result.redirects == 4 && result.status == 200 && strcmp(body_of(io), "done") == 0);
    assert(contains(smith_io_test_request(io, "r2.example", 443, 0), "GET /moved/file?q=2 HTTP/1.1"));
    assert(contains(smith_io_test_request(io, "r2.example", 443, 1), "GET /moved/dir/other HTTP/1.1"));
    assert(contains(smith_io_test_request(io, "r2.example", 443, 2), "GET /moved/dir/sibling.txt HTTP/1.1"));
    assert(contains(smith_io_test_request(io, "r3.example", 443, 0), "GET /last HTTP/1.1"));

    // Only https: a redirect to http, to file or to ftp is refused and nothing is requested from it.
    const char *schemes[] = {"http://s.example/x", "file:///etc/passwd", "ftp://s.example/x"};
    for (size_t i = 0; i < 3; i++) {
        char response[256];
        snprintf(response, sizeof response, "HTTP/1.1 302 Found\r\nLocation: %s\r\n\r\n", schemes[i]);
        serve(io, "t.example", response);
        result = get(io, "https://t.example/t");
        assert(result.error.code == EPROTONOSUPPORT && result.status == 302);
        assert(contains(result.error.message, schemes[i]) && contains(result.error.message, "only https"));
    }
    serve(io, "u.example", "HTTP/1.1 302 Found\r\n\r\n");
    result = get(io, "https://u.example/u");
    assert(result.error.code == EPROTO && contains(result.error.message, "no Location"));
}

static void check_redirect_loop(SmithIo *io) {
    for (size_t i = 0; i < 12; i++)
        serve(io, "loop.example", "HTTP/1.1 302 Found\r\nLocation: https://loop.example/again\r\n\r\n");
    SmithHttpResult result = get(io, "https://loop.example/start");
    assert(result.error.code == ELOOP && result.redirects == 10 && contains(result.error.message, "10 redirects"));
}

static void check_failures(SmithIo *io) {
    serve(io, "n.example", "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nnot found");
    SmithHttpResult result = get(io, "https://n.example/missing");
    assert(result.error.code == ENOENT && result.status == 404);
    assert(contains(result.error.message, "https://n.example/missing") &&
           contains(result.error.message, "HTTP 404 Not Found"));
    serve(io, "v.example", "HTTP/1.1 503 Service Unavailable\r\n\r\n");
    result = get(io, "https://v.example/v");
    assert(result.error.code == EPROTO && contains(result.error.message, "HTTP 503 Service Unavailable"));

    serve(io, "short.example", "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n0123456789");
    result = get(io, "https://short.example/s");
    assert(result.error.code == EIO && contains(result.error.message, "short body: 10 of 100 bytes"));
    assert(strcmp(body_of(io), "0123456789") == 0); // the file holds what arrived: the caller removes it

    const char *partial = "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n01234";
    SmithNetServer reset = {.host = "reset.example",
                            .port = 443,
                            .response = partial,
                            .response_size = strlen(partial),
                            .error = ECONNRESET};
    assert(smith_io_test_add_server(io, reset).code == 0);
    result = get(io, "https://reset.example/r");
    assert(result.error.code == ECONNRESET && contains(result.error.message, "reset.example"));

    SmithNetServer bad_tls = {.host = "tls.example", .port = 443, .tls_failure = "the certificate has expired"};
    assert(smith_io_test_add_server(io, bad_tls).code == 0);
    result = get(io, "https://tls.example/t");
    assert(result.error.code == EAUTH && contains(result.error.message, "the certificate has expired") &&
           result.status == 0);
    result = get(io, "https://nobody.example/t");
    assert(result.error.code == ECONNREFUSED);

    serve(io, "junk.example", "SSH-2.0-OpenSSH_9.0\r\n\r\n");
    result = get(io, "https://junk.example/j");
    assert(result.error.code == EPROTO && contains(result.error.message, "not HTTP"));
    serve(io, "cut.example", "HTTP/1.1 200 OK\r\nContent-Le");
    result = get(io, "https://cut.example/c");
    assert(result.error.code == EPROTO && contains(result.error.message, "before the response head ended"));

    serve(io, "bad.example", "HTTP/1.1 200 OK\r\nContent-Length: lots\r\n\r\n");
    result = get(io, "https://bad.example/b");
    assert(result.error.code == EPROTO && contains(result.error.message, "not a number"));
}

static void check_urls(SmithIo *io) {
    const char *refused[] = {"http://a.example/x",    "ftp://a.example/x",        "a.example/x",          "https://",
                             "https://u@a.example/x", "https://a.example:port/x", "https://a.example/a b"};
    for (size_t i = 0; i < sizeof refused / sizeof refused[0]; i++) {
        SmithHttpResult result = get(io, refused[i]);
        assert(result.error.code == EINVAL && contains(result.error.message, refused[i]));
    }
}

static void check_local(SmithIo *io) {
    assert(smith_files_write(&io->files, "source.bin", "local bytes", 11, SmithWrite_Replace).code == 0);
    char url[256];
    snprintf(url, sizeof url, "file://%s", "source.bin");
    SmithHttpResult result = get(io, url);
    assert(result.error.code == 0 && result.size == 11 && strcmp(body_of(io), "local bytes") == 0);
    result = get(io, "file://missing.bin");
    assert(result.error.code == ENOENT && contains(result.error.message, "file://missing.bin"));
}

// A body of several blocks, in the order the writes land: the file has every byte, the arena gets back what the call
// used, and the body was never all in memory at once (the arena of the real io is big, so this checks the accounting).
static void check_large(SmithIo *io) {
    static unsigned char response[3 << 20];
    int head = snprintf((char *)response, 128, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n", sizeof response - 128);
    size_t body = sizeof response - 128;
    for (size_t i = 0; i < body; i++)
        response[(size_t)head + i] = (unsigned char)(i * 31 + i / 251);
    SmithNetServer server = {.host = "big.example",
                             .port = 443,
                             .response = response,
                             .response_size = (size_t)head + body,
                             .chunk = 100000};
    assert(smith_io_test_add_server(io, server).code == 0);
    size_t used = io->arena.used;
    SmithHttpResult result = get(io, "https://big.example/big");
    assert(result.error.code == 0 && result.size == body && io->arena.used == used);
    SmithRead read = smith_files_read(&io->files, &io->arena, "download.bin");
    assert(read.error.code == 0 && read.size == body);
    for (size_t i = 0; i < body; i++)
        assert((unsigned char)read.data[i] == (unsigned char)(i * 31 + i / 251));
}

static void run_checks(SmithIo *io) {
    check_basics(io);
    check_chunked(io);
    check_redirects(io);
    check_redirect_loop(io);
    check_failures(io);
    check_urls(io);
    check_local(io);
}

int main(void) {
    static TestMemory memory;
    SmithIo fake = smith_io_make_test((SmithIoTest){.memory = memory.bytes, .capacity = sizeof memory.bytes});
    run_checks(&fake);
    SmithIo real = smith_io_make(0, nullptr);
    run_checks(&real);
    check_large(&real);
    smith_io_close(&real);
    puts("http: all checks passed");
    return 0;
}
