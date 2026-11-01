// Tests of packages (runtime/package.c) on a scripted network: the download is smith's HTTPS client over std::net,
// and its fake network, set as the thread's handler, stands in for the internet, so the cache, the files and tar are
// all real. Usage: package-test <archive.tar.gz> <scratch folder>

#ifdef __linux__
// runtime/package.c, included below, needs what glibc hides from C23 (nftw's FTW_PHYS), as in smith.c.
// NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming): glibc's name for it
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <stdio.h>

#define SMITH_ASSERT(condition) assert(condition)

#include "../runtime/package.c"

constexpr size_t test_archive_max = (size_t)1 << 20;

static unsigned char test_archive[test_archive_max];
static size_t test_archive_size;
static const char *test_scratch;
static int test_cache_count;

static bool contains(const char *text, const char *needle) {
    return strstr(text, needle) != nullptr;
}

static void load_archive(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file != nullptr);
    test_archive_size = fread(test_archive, 1, sizeof test_archive, file);
    assert(test_archive_size > 0 && test_archive_size < sizeof test_archive);
    fclose(file);
}

// A fresh cache folder for the case, named by XDG_CACHE_HOME.
static void fresh_cache(char packages[PATH_MAX]) {
    char cache[PATH_MAX];
    snprintf(cache, sizeof cache, "%s/cache-%d", test_scratch, test_cache_count++);
    setenv("XDG_CACHE_HOME", cache, 1);
    snprintf(packages, PATH_MAX, "%s/smith/packages", cache);
}

// The archive behind an HTTP head, as a server would send it; `claimed` is the Content-Length it announces. With
// `reset` the connection fails with a reset after the response instead of ending.
static void serve_archive(SmithNetworkFake *fake, const char *host, size_t claimed, bool reset) {
    static unsigned char response[test_archive_max + 256];
    int head = snprintf((char *)response, 256,
                        "HTTP/1.1 200 OK\r\nContent-Type: application/gzip\r\nContent-Length: %zu\r\n\r\n", claimed);
    memcpy(response + head, test_archive, test_archive_size);
    SmithNetworkServer server = {.host = host,
                                 .port = 443,
                                 .response = (const char *)response,
                                 .chunk = 20000,
                                 .response_size = (size_t)head + test_archive_size,
                                 .resets = reset};
    smith_network_fake_add(fake, server);
}

static void serve_text(SmithNetworkFake *fake, const char *host, const char *text) {
    smith_network_fake_add(fake, (SmithNetworkServer){.host = host, .port = 443, .response = text});
}

static size_t temporaries(const char *packages) {
    DIR *dir = opendir(packages);
    if (dir == nullptr) return 0;
    size_t count = 0;
    for (struct dirent *entry = readdir(dir); entry != nullptr; entry = readdir(dir))
        if (strncmp(entry->d_name, ".tmp", 4) == 0) count += 1;
    closedir(dir);
    return count;
}

static bool exists(const char *path) {
    struct stat info;
    return stat(path, &info) == 0;
}

static void archive_hash(char hash[smith_package_hash_size]) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/archive.bin", test_scratch);
    FILE *file = fopen(path, "wb");
    assert(file != nullptr && fwrite(test_archive, 1, test_archive_size, file) == test_archive_size);
    fclose(file);
    char hex[smith_sha256_hex_size];
    assert(smith_sha256_file(path, hex));
    snprintf(hash, smith_package_hash_size, "sha256:%s", hex);
}

typedef struct {
    SmithNetworkFake net;
    SmithNetHandler *outside; // the handler the case replaced
    char packages[PATH_MAX];
    char hash[smith_package_hash_size];
    char folder[PATH_MAX];
    char actual[smith_package_hash_size];
    SmithPackageFault error;
} Case;

static void start_case(Case *c) {
    *c = (Case){.net = smith_network_fake()};
    c->outside = smith_network_set_handler(&c->net.base);
    fresh_cache(c->packages);
    archive_hash(c->hash);
}

static void end_case(Case *c) {
    (void)smith_network_set_handler(c->outside);
}

// What the case sent on the connection of the id (the n-th connect of the case is id n), as a text.
static const char *request(const Case *c, int id) {
    static char text[1024];
    struct smith_array_u8_n sent = smith_network_fake_sent(&c->net, id);
    size_t size = sent.count[0] < sizeof text - 1 ? sent.count[0] : sizeof text - 1;
    memcpy(text, sent.data, size);
    text[size] = '\0';
    return text;
}

// Whether anything was sent on the connection of the id: false when the network was never asked.
static bool was_asked(const Case *c, int id) {
    return smith_network_fake_sent(&c->net, id).count[0] != 0;
}

static bool fetch(Case *c, const char *url, const char *wanted) {
    return smith_package_ensure(url, wanted, false, c->folder, c->actual, &c->error);
}

// Nothing complete is under the hash, and no temporary file is left behind.
static void check_nothing_left(const Case *c, const char *hash) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", c->packages, hash + 7);
    assert(!exists(path));
    assert(temporaries(c->packages) == 0);
}

static void check_installed(const Case *c) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/hello.txt", c->folder);
    assert(exists(path));
    snprintf(path, sizeof path, "%s/%s", c->packages, c->hash + 7);
    assert(strcmp(c->folder, path) == 0);
    assert(temporaries(c->packages) == 0);
}

static void test_download(void) {
    Case c;
    start_case(&c);
    serve_archive(&c.net, "pkg.example", test_archive_size, false);
    assert(fetch(&c, "https://pkg.example/a/pkg.tar.gz", c.hash));
    check_installed(&c);
    assert(strncmp(request(&c, 1), "GET /a/pkg.tar.gz HTTP/1.1\r\n", 28) == 0);
    // A package in the cache is not fetched again: no server is left, and the connection would be refused.
    assert(fetch(&c, "https://pkg.example/a/pkg.tar.gz", c.hash));
    assert(!was_asked(&c, 2));
    end_case(&c);
}

static void test_fetch_without_a_hash(void) {
    Case c;
    start_case(&c);
    serve_archive(&c.net, "pkg.example", test_archive_size, false);
    assert(fetch(&c, "https://pkg.example/pkg.tar.gz", nullptr)); // smith fetch: the hash is the answer
    assert(strcmp(c.actual, c.hash) == 0);
    check_installed(&c);
    end_case(&c);
}

static void test_redirects(void) {
    Case c;
    start_case(&c);
    serve_text(&c.net, "pkg.example",
               "HTTP/1.1 302 Found\r\nLocation: https://cdn.example/blob/1\r\nContent-Length: 0\r\n\r\n");
    serve_text(&c.net, "cdn.example", "HTTP/1.1 301 Moved\r\nLocation: /blob/2\r\n\r\n");
    serve_archive(&c.net, "cdn.example", test_archive_size, false);
    assert(fetch(&c, "https://pkg.example/pkg.tar.gz", c.hash));
    check_installed(&c);
    assert(strncmp(request(&c, 3), "GET /blob/2 HTTP/1.1", 20) == 0);
    end_case(&c);

    // A redirect to another scheme is refused: only https is followed, as for the first URL.
    const char *schemes[] = {"http://cdn.example/blob", "file:///etc/hosts"};
    for (size_t i = 0; i < 2; i++) {
        start_case(&c);
        char text[256];
        snprintf(text, sizeof text, "HTTP/1.1 302 Found\r\nLocation: %s\r\nContent-Length: 0\r\n\r\n", schemes[i]);
        serve_text(&c.net, "pkg.example", text);
        serve_archive(&c.net, "cdn.example", test_archive_size, false);
        assert(!fetch(&c, "https://pkg.example/pkg.tar.gz", c.hash));
        assert(contains(c.error.text, "only https") && contains(c.error.text, schemes[i]));
        assert(!was_asked(&c, 2)); // cdn.example was never asked
        check_nothing_left(&c, c.hash);
        end_case(&c);
    }
}

static void test_failures(void) {
    Case c;
    start_case(&c);
    serve_text(&c.net, "pkg.example", "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    assert(!fetch(&c, "https://pkg.example/missing.tar.gz", c.hash));
    assert(contains(c.error.text, "https://pkg.example/missing.tar.gz") &&
           contains(c.error.text, "HTTP 404 Not Found"));
    check_nothing_left(&c, c.hash);
    end_case(&c);

    // A body shorter than it said, and a connection that breaks halfway: nothing is installed or left over.
    start_case(&c);
    serve_archive(&c.net, "pkg.example", test_archive_size + 1000, false);
    assert(!fetch(&c, "https://pkg.example/pkg.tar.gz", c.hash));
    assert(contains(c.error.text, "short body"));
    check_nothing_left(&c, c.hash);
    end_case(&c);

    start_case(&c);
    serve_archive(&c.net, "pkg.example", test_archive_size, true);
    assert(fetch(&c, "https://pkg.example/pkg.tar.gz", c.hash)); // the body was whole before the reset: fine
    end_case(&c);
    start_case(&c);
    serve_archive(&c.net, "pkg.example", test_archive_size + 10, true);
    assert(!fetch(&c, "https://pkg.example/pkg.tar.gz", c.hash));
    assert(contains(c.error.text, "pkg.example"));
    check_nothing_left(&c, c.hash);
    end_case(&c);

    start_case(&c);
    smith_network_fake_add(
        &c.net,
        (SmithNetworkServer){.host = "pkg.example", .port = 443, .fails = true, .error = SmithNetworkError_Expired});
    assert(!fetch(&c, "https://pkg.example/pkg.tar.gz", c.hash));
    assert(contains(c.error.text, "the certificate has expired"));
    check_nothing_left(&c, c.hash);
    end_case(&c);
}

static void test_wrong_hash(void) {
    Case c;
    start_case(&c);
    char wrong[smith_package_hash_size];
    snprintf(wrong, sizeof wrong, "sha256:%064d", 7);
    serve_archive(&c.net, "pkg.example", test_archive_size, false);
    assert(!fetch(&c, "https://pkg.example/pkg.tar.gz", wrong));
    assert(contains(c.error.text, "https://pkg.example/pkg.tar.gz") && contains(c.error.text, wrong));
    assert(contains(c.error.text, c.hash));
    check_nothing_left(&c, wrong);
    check_nothing_left(&c, c.hash);
    end_case(&c);
}

static void test_offline_and_leftovers(void) {
    Case c;
    start_case(&c);
    serve_archive(&c.net, "pkg.example", test_archive_size, false);
    assert(!smith_package_ensure("https://pkg.example/pkg.tar.gz", c.hash, true, c.folder, c.actual, &c.error));
    assert(contains(c.error.text, "https://pkg.example/pkg.tar.gz") && contains(c.error.text, "OFFLINE"));
    assert(!was_asked(&c, 1)); // nothing was asked of the network
    end_case(&c);

    // What a killed fetch leaves is a temporary name; the next fetch clears it and installs the package.
    start_case(&c);
    assert(smith_package_make_folders(c.packages));
    char stale[PATH_MAX];
    snprintf(stale, sizeof stale, "%s/.tmp-%d-archive", c.packages, (int)getpid());
    FILE *file = fopen(stale, "wb");
    assert(file != nullptr && fputs("half of an archive", file) >= 0);
    fclose(file);
    serve_archive(&c.net, "pkg.example", test_archive_size, false);
    assert(fetch(&c, "https://pkg.example/pkg.tar.gz", c.hash));
    check_installed(&c);
    end_case(&c);
}

int main(int argc, char **argv) {
    assert(argc == 3);
    // The fake network keeps what it sends and receives in memory from the current allocator; an arena is that memory.
    alignas(smith_arena_align_max) static unsigned char pool[(size_t)64 << 20];
    SmithArena memory = smith_arena_fixed(pool, sizeof pool);
    SmithArenaAllocator allocator = smith_arena_allocator(&memory);
    (void)smith_mem_set_allocator(&allocator.base);
    load_archive(argv[1]);
    test_scratch = argv[2];
    test_download();
    test_fetch_without_a_hash();
    test_redirects();
    test_failures();
    test_wrong_hash();
    test_offline_and_leftovers();
    puts("package: all checks passed");
    return 0;
}
