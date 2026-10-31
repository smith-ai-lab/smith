// The scheduler handler (issue #936, lib/handlers.c): with one set whose `io` is set, read, write, pread, pwrite,
// readv, writev, send, recv, accept and connect become a request handed to it, and the caller gets its result and
// errno; with none set (or none with `io`) they are the real calls; a files handler set beside it still answers read
// and write of its descriptors. Prints `ok name` or `FAIL name`. The scheduler is set only around each call, so printf
// is the real one.
#include "smith/handlers.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

static int failures = 0;

static void check(const char *name, bool good) {
    printf("%s %s\n", good ? "ok" : "FAIL", name);
    if (!good) failures += 1;
}

// A scheduler that logs each request it sees and answers it with what the test chose.
typedef struct {
    SmithScheduler scheduler;
    SmithIoRequest log[16];
    int calls;
    ssize_t answer;
    int answer_error;
} Fake;

static void fake_io(SmithScheduler *self, SmithIoRequest *request) {
    Fake *fake = (Fake *)self;
    if (fake->calls < 16) fake->log[fake->calls] = *request;
    fake->calls += 1;
    request->result = fake->answer;
    request->error = fake->answer < 0 ? fake->answer_error : 0;
}

static bool is(const SmithIoRequest *request, int operation, int fd, size_t size) {
    return request->operation == operation && request->fd == fd && request->size == size;
}

static void routes_each_call(void) {
    Fake fake = {.scheduler = {.io = fake_io}, .answer = 3};
    char buffer[16] = "";
    struct iovec vector = {buffer, 4};
    struct sockaddr peer = {};
    socklen_t length = sizeof peer;
    ssize_t results[10];
    SmithScheduler *outside = smith_handlers_set_scheduler(&fake.scheduler);
    results[0] = read(7, buffer, 5);
    results[1] = write(8, "hello!", 6);
    results[2] = pread(9, buffer, 4, 100);
    results[3] = pwrite(9, "ab", 2, 7);
    results[4] = readv(10, &vector, 1);
    results[5] = writev(10, &vector, 1);
    results[6] = send(11, "hello", 5, 0);
    results[7] = recv(11, buffer, 5, 0);
    results[8] = accept(12, &peer, &length);
    results[9] = connect(13, &peer, length);
    smith_handlers_set_scheduler(outside);

    check("scheduler: ten calls reached io, one request each", fake.calls == 10);
    bool each = true;
    for (int i = 0; i < 10; i++)
        each = each && results[i] == 3;
    check("scheduler: each caller got the result io answered", each);
    check("scheduler: read becomes a Read request with the fd and count",
          is(&fake.log[0], SmithIoOperation_Read, 7, 5));
    check("scheduler: write becomes a Write request", is(&fake.log[1], SmithIoOperation_Write, 8, 6));
    check("scheduler: pread becomes a Pread request with the offset",
          is(&fake.log[2], SmithIoOperation_Pread, 9, 4) && fake.log[2].offset == 100);
    check("scheduler: pwrite becomes a Pwrite request with the offset",
          is(&fake.log[3], SmithIoOperation_Pwrite, 9, 2) && fake.log[3].offset == 7);
    check("scheduler: readv becomes a Readv request with the vectors",
          fake.log[4].operation == SmithIoOperation_Readv && fake.log[4].fd == 10 && fake.log[4].vectors == &vector &&
              fake.log[4].count == 1);
    check("scheduler: writev becomes a Writev request", fake.log[5].operation == SmithIoOperation_Writev);
    check("scheduler: send becomes a Send request", is(&fake.log[6], SmithIoOperation_Send, 11, 5));
    check("scheduler: recv becomes a Recv request", is(&fake.log[7], SmithIoOperation_Recv, 11, 5));
    check("scheduler: accept becomes an Accept request with the address",
          fake.log[8].operation == SmithIoOperation_Accept && fake.log[8].fd == 12 && fake.log[8].address == &peer &&
              fake.log[8].address_length == &length);
    check("scheduler: connect becomes a Connect request with the address and its length",
          fake.log[9].operation == SmithIoOperation_Connect && fake.log[9].fd == 13 && fake.log[9].peer == &peer &&
              fake.log[9].peer_length == length);
}

static void sets_errno_from_a_failed_request(void) {
    Fake fake = {.scheduler = {.io = fake_io}, .answer = -1, .answer_error = EAGAIN};
    char buffer[4];
    SmithScheduler *outside = smith_handlers_set_scheduler(&fake.scheduler);
    errno = 0;
    ssize_t result = read(7, buffer, 4);
    int error = errno;
    smith_handlers_set_scheduler(outside);
    check("scheduler: a failed request returns -1 and sets errno", result == -1 && error == EAGAIN);
}

static void is_the_real_call_with_none_set(void) {
    int pipes[2];
    char buffer[8] = "";
    check("scheduler: none is set at first", smith_handlers_scheduler() == nullptr);
    if (pipe(pipes) != 0) abort();
    check("scheduler: none set, write and read are the real ones",
          write(pipes[1], "abc", 3) == 3 && read(pipes[0], buffer, 8) == 3 && memcmp(buffer, "abc", 3) == 0);
    char name[] = "/tmp/smith_scheduler_XXXXXX";
    int file = mkstemp(name);
    unlink(name);
    struct iovec parts[2] = {{"ab", 2}, {"cd", 2}};
    check("scheduler: none set, pwrite, pread and writev are the real ones",
          pwrite(file, "xy", 2, 0) == 2 && pread(file, buffer, 2, 0) == 2 && memcmp(buffer, "xy", 2) == 0 &&
              writev(pipes[1], parts, 2) == 4);
    char first[2] = "";
    char second[2] = "";
    struct iovec into[2] = {{first, 2}, {second, 2}};
    check("scheduler: none set, readv is the real one", readv(pipes[0], into, 2) == 4 && first[0] == 'a');
    close(file);
    close(pipes[0]);
    close(pipes[1]);
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) abort();
    check("scheduler: none set, send and recv are the real ones",
          send(pair[0], "hi", 2, 0) == 2 && recv(pair[1], buffer, 8, 0) == 2 && buffer[0] == 'h');
    close(pair[0]);
    close(pair[1]);
    check("scheduler: none set, connect and accept fail as the real ones do on a bad descriptor",
          connect(-1, nullptr, 0) == -1 && errno == EBADF && accept(-1, nullptr, nullptr) == -1 && errno == EBADF);
}

static void without_io_is_the_real_call(void) {
    SmithScheduler spawn_only = {};
    SmithScheduler *outside = smith_handlers_set_scheduler(&spawn_only);
    int pipes[2];
    char buffer[4] = "";
    if (pipe(pipes) != 0) abort();
    bool good = write(pipes[1], "q", 1) == 1 && read(pipes[0], buffer, 4) == 1 && buffer[0] == 'q';
    SmithScheduler *seen = smith_handlers_scheduler();
    smith_handlers_set_scheduler(outside);
    close(pipes[0]);
    close(pipes[1]);
    check("scheduler: one without io leaves the calls to the real ones", good && seen == &spawn_only);
}

typedef struct {
    SmithFileHandler files;
    int reads;
} FakeFiles;

static ssize_t files_read(SmithFileHandler *self, int fd, void *buffer, size_t size) {
    (void)fd;
    ((FakeFiles *)self)->reads += 1;
    memset(buffer, 'f', size);
    return (ssize_t)size;
}

static int files_open(SmithFileHandler *self, const char *path, int flags, int mode) {
    (void)self, (void)path, (void)flags, (void)mode;
    return 40;
}

static void a_files_handler_still_answers_its_read(void) {
    Fake fake = {.scheduler = {.io = fake_io}, .answer = 99};
    FakeFiles files = {.files = {.open = files_open, .read = files_read}};
    SmithFileHandler *outside_files = smith_handlers_set_files(&files.files);
    char buffer[4] = "";
    SmithScheduler *outside = smith_handlers_set_scheduler(&fake.scheduler);
    ssize_t result = read(40, buffer, 4);
    smith_handlers_set_scheduler(outside);
    smith_handlers_set_files(outside_files);
    check("scheduler: a files handler answers read of a file; the scheduler never sees it",
          result == 4 && files.reads == 1 && fake.calls == 0 && buffer[0] == 'f');
}

int main(void) {
    routes_each_call();
    sets_errno_from_a_failed_request();
    is_the_real_call_with_none_set();
    without_io_is_the_real_call();
    a_files_handler_still_answers_its_read();
    return failures == 0 ? 0 : 1;
}
