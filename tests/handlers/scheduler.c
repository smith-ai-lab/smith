// The descriptor calls are routed by what the descriptor is (issue #2662, lib/handlers.c): 0, 1 and 2 go to the console
// handler, a socket (also one that C code smith did not write opened) goes to the net handler, any other descriptor
// goes to the files handler; a handler that is set always wins; none set is the real call. A handler with
// `descriptors_only` (the scheduler's) answers those calls and nothing else, and is not used while the thread holds a
// pthread mutex. Prints `ok name` or `FAIL name`. A handler is set only around each call, so printf is the real one.
#include "smith/handlers.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
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

// Fakes that count each call they see, answer 3 (or -1 with EAGAIN in `refuse` mode) and remember the last descriptor.
typedef struct {
    SmithFileHandler files;
    SmithNetHandler net;
    SmithConsoleHandler console;
    int files_calls;
    int net_calls;
    int console_calls;
    int last_fd;
    off_t last_offset;
    bool refuse;
} Fakes;

static Fakes *fakes_of_files(SmithFileHandler *self) {
    return (Fakes *)((char *)self - offsetof(Fakes, files));
}

static Fakes *fakes_of_net(SmithNetHandler *self) {
    return (Fakes *)((char *)self - offsetof(Fakes, net));
}

static Fakes *fakes_of_console(SmithConsoleHandler *self) {
    return (Fakes *)((char *)self - offsetof(Fakes, console));
}

static ssize_t answer(Fakes *fakes) {
    if (!fakes->refuse) return 3;
    errno = EAGAIN;
    return -1;
}

static ssize_t files_read(SmithFileHandler *self, int fd, void *buffer, size_t size) {
    (void)buffer, (void)size;
    Fakes *fakes = fakes_of_files(self);
    fakes->files_calls += 1;
    fakes->last_fd = fd;
    return answer(fakes);
}

static ssize_t files_write(SmithFileHandler *self, int fd, const void *buffer, size_t size) {
    return files_read(self, fd, (void *)(uintptr_t)buffer, size);
}

static ssize_t files_pread(SmithFileHandler *self, int fd, void *buffer, size_t size, off_t offset) {
    fakes_of_files(self)->last_offset = offset;
    return files_read(self, fd, buffer, size);
}

static ssize_t files_pwrite(SmithFileHandler *self, int fd, const void *buffer, size_t size, off_t offset) {
    fakes_of_files(self)->last_offset = offset;
    return files_read(self, fd, (void *)(uintptr_t)buffer, size);
}

static ssize_t files_readv(SmithFileHandler *self, int fd, const struct iovec *vectors, int count) {
    (void)vectors, (void)count;
    return files_read(self, fd, nullptr, 0);
}

static int files_open(SmithFileHandler *self, const char *path, int flags, int mode) {
    (void)self, (void)path, (void)flags, (void)mode;
    return 40;
}

static ssize_t net_read(SmithNetHandler *self, int fd, void *buffer, size_t size) {
    (void)buffer, (void)size;
    Fakes *fakes = fakes_of_net(self);
    fakes->net_calls += 1;
    fakes->last_fd = fd;
    return answer(fakes);
}

static ssize_t net_write(SmithNetHandler *self, int fd, const void *buffer, size_t size) {
    return net_read(self, fd, (void *)(uintptr_t)buffer, size);
}

static ssize_t net_send(SmithNetHandler *self, int fd, const void *buffer, size_t size, int flags) {
    (void)flags;
    return net_read(self, fd, (void *)(uintptr_t)buffer, size);
}

static ssize_t net_recv(SmithNetHandler *self, int fd, void *buffer, size_t size, int flags) {
    (void)flags;
    return net_read(self, fd, buffer, size);
}

static int net_accept(SmithNetHandler *self, int fd, struct sockaddr *address, socklen_t *length) {
    (void)address, (void)length;
    return (int)net_read(self, fd, nullptr, 0);
}

static int net_connect(SmithNetHandler *self, int fd, const struct sockaddr *peer, socklen_t length) {
    (void)peer, (void)length;
    return (int)net_read(self, fd, nullptr, 0);
}

static ssize_t console_write(SmithConsoleHandler *self, int fd, const void *buffer, size_t size) {
    (void)buffer, (void)size;
    Fakes *fakes = fakes_of_console(self);
    fakes->console_calls += 1;
    fakes->last_fd = fd;
    return answer(fakes);
}

static ssize_t console_read(SmithConsoleHandler *self, void *buffer, size_t size) {
    (void)buffer, (void)size;
    Fakes *fakes = fakes_of_console(self);
    fakes->console_calls += 1;
    fakes->last_fd = 0;
    return answer(fakes);
}

static Fakes make_fakes(bool descriptors_only) {
    return (Fakes){
        .files = {.open = files_open,
                  .read = files_read,
                  .write = files_write,
                  .pwrite = files_pwrite,
                  .pread = files_pread,
                  .readv = files_readv,
                  .writev = files_readv,
                  .descriptors_only = descriptors_only},
        .net = {.read = net_read,
                .write = net_write,
                .send = net_send,
                .recv = net_recv,
                .accept_socket = net_accept,
                .connect_socket = net_connect,
                .descriptors_only = descriptors_only},
        .console = {.write = console_write, .read = console_read, .descriptors_only = descriptors_only},
    };
}

typedef struct {
    SmithFileHandler *files;
    SmithNetHandler *net;
    SmithConsoleHandler *console;
} Outside;

static Outside install(Fakes *fakes) {
    return (Outside){smith_handlers_set_files(&fakes->files), smith_handlers_set_net(&fakes->net),
                     smith_handlers_set_console(&fakes->console)};
}

static void restore(Outside outside) {
    smith_handlers_set_files(outside.files);
    smith_handlers_set_net(outside.net);
    smith_handlers_set_console(outside.console);
}

static void a_socket_goes_to_net_and_a_file_to_files(void) {
    Fakes fakes = make_fakes(false);
    int pair[2];
    int pipes[2];
    char buffer[16] = "";
    struct iovec vector = {buffer, 4};
    struct sockaddr peer = {};
    socklen_t length = sizeof peer;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0 || pipe(pipes) != 0) abort();
    Outside outside = install(&fakes);
    ssize_t results[8];
    results[0] = read(pair[0], buffer, 5);
    results[1] = write(pair[0], "hello", 5);
    results[2] = send(pair[0], "hello", 5, 0);
    results[3] = recv(pair[0], buffer, 5, 0);
    results[4] = accept(pair[0], &peer, &length);
    results[5] = connect(pair[0], &peer, length);
    int net_calls = fakes.net_calls;
    int net_fd = fakes.last_fd;
    int files_before = fakes.files_calls;
    results[6] = read(pipes[0], buffer, 5);
    results[7] = write(pipes[1], "hello", 5);
    ssize_t positioned = pread(pipes[0], buffer, 4, 100);
    off_t offset = fakes.last_offset;
    ssize_t positioned_write = pwrite(pipes[1], "ab", 2, 7);
    ssize_t vectored = readv(pipes[0], &vector, 1);
    ssize_t vectored_write = writev(pipes[1], &vector, 1);
    restore(outside);
    bool six = true;
    for (int i = 0; i < 6; i++)
        six = six && results[i] == 3;
    check("routing: six calls on a socket reach the net handler, with the answers it gave", six && net_calls == 6);
    check("routing: the net handler is given the descriptor of the socket", net_fd == pair[0]);
    check("routing: no call on a socket reached the files handler", files_before == 0);
    check("routing: read and write on a pipe reach the files handler", results[6] == 3 && results[7] == 3);
    check("routing: pread, pwrite, readv and writev on a pipe reach the files handler, with the offset",
          positioned == 3 && offset == 100 && positioned_write == 3 && vectored == 3 && vectored_write == 3 &&
              fakes.last_offset == 7 && fakes.files_calls == 6 && fakes.net_calls == 6);
    close(pair[0]);
    close(pair[1]);
    close(pipes[0]);
    close(pipes[1]);
}

static void the_console_gets_descriptors_0_1_and_2(void) {
    Fakes fakes = make_fakes(false);
    char buffer[8] = "";
    Outside outside = install(&fakes);
    ssize_t in = read(0, buffer, 4);
    ssize_t out = write(1, "x", 1);
    int out_fd = fakes.last_fd;
    ssize_t error = write(2, "x", 1);
    int error_fd = fakes.last_fd;
    restore(outside);
    check("routing: read(0), write(1) and write(2) reach the console handler",
          in == 3 && out == 3 && error == 3 && fakes.console_calls == 3 && out_fd == 1 && error_fd == 2 &&
              fakes.files_calls == 0 && fakes.net_calls == 0);
}

static void a_failed_answer_is_returned_with_its_errno(void) {
    Fakes fakes = make_fakes(false);
    fakes.refuse = true;
    int pair[2];
    char buffer[4];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) abort();
    Outside outside = install(&fakes);
    errno = 0;
    ssize_t result = recv(pair[0], buffer, 4, 0);
    int error = errno;
    restore(outside);
    close(pair[0]);
    close(pair[1]);
    check("routing: a failed answer returns -1 and its errno", result == -1 && error == EAGAIN);
}

static void a_handler_that_is_not_set_leaves_the_call_real(void) {
    Fakes fakes = make_fakes(false);
    int pair[2];
    int pipes[2];
    char buffer[8] = "";
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0 || pipe(pipes) != 0) abort();
    SmithFileHandler *outside_files = smith_handlers_set_files(&fakes.files);
    bool socket_real = send(pair[0], "hi", 2, 0) == 2 && recv(pair[1], buffer, 8, 0) == 2 && buffer[0] == 'h';
    smith_handlers_set_files(outside_files);
    SmithNetHandler *outside_net = smith_handlers_set_net(&fakes.net);
    bool pipe_real = write(pipes[1], "abc", 3) == 3 && read(pipes[0], buffer, 8) == 3 && memcmp(buffer, "abc", 3) == 0;
    smith_handlers_set_net(outside_net);
    check("routing: a socket with only a files handler set is the real call", socket_real && fakes.files_calls == 0);
    check("routing: a pipe with only a net handler set is the real call", pipe_real && fakes.net_calls == 0);
    close(pair[0]);
    close(pair[1]);
    close(pipes[0]);
    close(pipes[1]);
}

static void none_set_is_the_real_call(void) {
    int pipes[2];
    char buffer[8] = "";
    if (pipe(pipes) != 0) abort();
    check("routing: none is set at first",
          smith_handlers_files() == nullptr && smith_handlers_net() == nullptr && smith_handlers_console() == nullptr);
    check("routing: none set, write and read are the real ones",
          write(pipes[1], "abc", 3) == 3 && read(pipes[0], buffer, 8) == 3 && memcmp(buffer, "abc", 3) == 0);
    char name[] = "/tmp/smith_scheduler_XXXXXX";
    int file = mkstemp(name);
    unlink(name);
    struct iovec parts[2] = {{"ab", 2}, {"cd", 2}};
    check("routing: none set, pwrite, pread and writev are the real ones",
          pwrite(file, "xy", 2, 0) == 2 && pread(file, buffer, 2, 0) == 2 && memcmp(buffer, "xy", 2) == 0 &&
              writev(pipes[1], parts, 2) == 4);
    char first[2] = "";
    char second[2] = "";
    struct iovec into[2] = {{first, 2}, {second, 2}};
    check("routing: none set, readv is the real one", readv(pipes[0], into, 2) == 4 && first[0] == 'a');
    close(file);
    close(pipes[0]);
    close(pipes[1]);
    check("routing: none set, connect and accept fail as the real ones do on a bad descriptor",
          connect(-1, nullptr, 0) == -1 && errno == EBADF && accept(-1, nullptr, nullptr) == -1 && errno == EBADF);
}

static void a_fake_files_descriptor_that_is_not_open_goes_to_files(void) {
    Fakes fakes = make_fakes(false);
    char buffer[4] = "";
    SmithFileHandler *outside = smith_handlers_set_files(&fakes.files);
    ssize_t result = read(40, buffer, 4);
    smith_handlers_set_files(outside);
    check("routing: a descriptor the system does not have is a file's, and the files handler answers it",
          result == 3 && fakes.files_calls == 1 && fakes.last_fd == 40);
}

static void a_descriptors_only_handler_answers_calls_and_nothing_else(void) {
    Fakes fakes = make_fakes(true);
    int pipes[2];
    char buffer[8] = "";
    if (pipe(pipes) != 0) abort();
    Outside outside = install(&fakes);
    bool none_seen =
        smith_handlers_files() == nullptr && smith_handlers_net() == nullptr && smith_handlers_console() == nullptr;
    ssize_t written = write(pipes[1], "abc", 3);
    int opened = open("/dev/null", O_RDONLY); // open is the real one: the handler answers descriptor calls only
    restore(outside);
    check("routing: a descriptors-only handler is not the one the getters report", none_seen);
    check("routing: a descriptors-only files handler answers write", written == 3 && fakes.files_calls == 1);
    check("routing: and leaves open to libc", opened > 2 && fakes.files_calls == 1);
    if (opened >= 0) close(opened);
    close(pipes[0]);
    close(pipes[1]);
    (void)buffer;
}

static void a_descriptors_only_handler_is_not_used_under_a_pthread_mutex(void) {
    Fakes fakes = make_fakes(true);
    SmithScheduler scheduler = {};
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    int pipes[2];
    char buffer[8] = "";
    if (pipe(pipes) != 0) abort();
    SmithScheduler *outside_scheduler = smith_handlers_set_scheduler(&scheduler);
    Outside outside = install(&fakes);
    pthread_mutex_lock(&mutex);
    ssize_t wrote = write(pipes[1], "xyz", 3);
    ssize_t held = read(pipes[0], buffer, 8);
    int during = fakes.files_calls;
    pthread_mutex_unlock(&mutex);
    ssize_t after = read(pipes[0], buffer, 8);
    int tried = pthread_mutex_trylock(&mutex);
    ssize_t wrote_again = write(pipes[1], "uv", 2);
    ssize_t under_try = read(pipes[0], buffer, 8);
    int during_try = fakes.files_calls;
    pthread_mutex_unlock(&mutex);
    ssize_t last = read(pipes[0], buffer, 8);
    restore(outside);
    smith_handlers_set_scheduler(outside_scheduler);
    close(pipes[0]);
    close(pipes[1]);
    check("routing: under a held pthread mutex write and read are the real ones",
          wrote == 3 && held == 3 && during == 0);
    check("routing: after the unlock the read reaches the handler", after == 3);
    check("routing: a successful trylock counts as held",
          tried == 0 && wrote_again == 2 && under_try == 2 && during_try == 1);
    check("routing: after the last unlock reads reach the handler again", last == 3 && fakes.files_calls == 2);
}

static void the_console_wins_for_descriptor_1_whatever_the_others_are(void) {
    Fakes fakes = make_fakes(false);
    Outside outside = install(&fakes);
    ssize_t written = write(1, "x", 1);
    restore(outside);
    check("routing: the console handler wins for descriptor 1 whatever the others are",
          written == 3 && fakes.files_calls == 0);
}

int main(void) {
    none_set_is_the_real_call();
    a_socket_goes_to_net_and_a_file_to_files();
    the_console_gets_descriptors_0_1_and_2();
    a_failed_answer_is_returned_with_its_errno();
    a_handler_that_is_not_set_leaves_the_call_real();
    a_fake_files_descriptor_that_is_not_open_goes_to_files();
    a_descriptors_only_handler_answers_calls_and_nothing_else();
    a_descriptors_only_handler_is_not_used_under_a_pthread_mutex();
    the_console_wins_for_descriptor_1_whatever_the_others_are();
    return failures == 0 ? 0 : 1;
}
