// The checks of <smith/io.h> that need the real world, run by tests/io.sh in a scratch folder (they write one folder up
// too). The fake io's checks (console, clock, programs, children, files in memory, the scripted network) are in
// tests/io_test.smith, on smith's test runner. With no argument, the checks below; with an argument, one program that
// reads the world and prints what it saw:
//   lines   reads stdin to the end with smith_console_read_line and prints "N lines, last=..."
//   seed    prints the seed and the first draw of the key
//   memory  prints how much of the arena's reservation counts as resident before and after it is touched

#include <smith/io.h>

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

constexpr size_t test_rounds_max = 100000;
constexpr uint64_t test_millisecond = 1'000'000;

typedef struct {
    alignas(smith_arena_align_max) unsigned char bytes[1 << 20];
} TestMemory;

static bool test_contains(const char *text, const char *needle) {
    return strstr(text, needle) != nullptr;
}

// ---------------------------------------------------------------- code that runs on any io

typedef struct {
    size_t lines;
    size_t bytes;
    int error;
    char message[192];
} Summary;

// Code under test: it is given files and an arena, so it cannot tell a real disk from a fake one.
static Summary summarize(SmithFiles *files, SmithArena *arena, const char *path) {
    Summary summary = {};
    SmithRead text = smith_files_read(files, arena, path);
    summary.error = text.error.code;
    memcpy(summary.message, text.error.message, sizeof summary.message);
    summary.bytes = text.data.count[0];
    for (size_t i = 0; i < text.data.count[0]; i++)
        if (((const char *)text.data.data)[i] == '\n') summary.lines += 1;
    return summary;
}

static bool summary_equal(const Summary *a, const Summary *b) {
    return a->lines == b->lines && a->bytes == b->bytes && a->error == b->error && strcmp(a->message, b->message) == 0;
}

// ---------------------------------------------------------------- real io

static void test_real_files(SmithIo *io) {
    const char text[] = "hello\nworld\n";
    SmithError error =
        smith_files_write(&io->files, "hello.txt",
                          (struct smith_array_const_u8_n){.data = (const uint8_t *)(text), .count = {sizeof text - 1}},
                          SmithWrite_Replace);
    assert(error.code == 0);
    SmithRead read = smith_files_read(&io->files, &io->arena, "hello.txt");
    assert(read.error.code == 0 && read.data.count[0] == sizeof text - 1 &&
           strcmp((const char *)read.data.data, text) == 0);

    SmithRead missing = smith_files_read(&io->files, &io->arena, "missing.txt");
    assert(missing.error.code == ENOENT);
    assert(strcmp(missing.error.message, "open missing.txt: No such file or directory") == 0);
    assert(missing.data.data == nullptr && missing.data.count[0] == 0);

    SmithRead hosts = smith_files_read(&io->files, &io->arena, "/etc/hosts"); // absolute paths work
    assert(hosts.error.code == 0 && hosts.data.count[0] > 0);
    assert(smith_files_write(&io->files, "../io-test-parent.txt",
                             (struct smith_array_const_u8_n){.data = (const uint8_t *)("parent"), .count = {6}},
                             SmithWrite_Replace)
               .code == 0);
    SmithRead parent = smith_files_read(&io->files, &io->arena, "../io-test-parent.txt"); // so do ".." paths
    assert(parent.error.code == 0 && strcmp((const char *)parent.data.data, "parent") == 0);
    assert(smith_files_read(&io->files, &io->arena, "").error.code == ENOENT);
    assert(smith_files_write(&io->files, "no/such/folder/x",
                             (struct smith_array_const_u8_n){.data = (const uint8_t *)(text), .count = {1}},
                             SmithWrite_Replace)
               .code == ENOENT);
}

static void test_real_big_file(SmithIo *io) {
    constexpr size_t size = (size_t)3 << 20;
    unsigned char *data = smith_arena_push_array(&io->arena, sizeof(unsigned char), size, alignof(unsigned char));
    for (size_t i = 0; i < size; i++)
        data[i] = (unsigned char)(i * 7 + 1);
    assert(smith_files_write(&io->files, "big.bin",
                             (struct smith_array_const_u8_n){.data = (const uint8_t *)(data), .count = {size}},
                             SmithWrite_Replace)
               .code == 0);
    SmithRead read = smith_files_read(&io->files, &io->arena, "big.bin");
    assert(read.error.code == 0 && read.data.count[0] == size && memcmp(read.data.data, data, size) == 0);
}

// Several file operations in one loop: they run in the order submitted, so the read sees the write before it.
static void test_real_async_files(SmithIo *io) {
    SmithOp write = {}, read = {}, missing = {};
    smith_files_write_async(&io->files, &write, "async.txt",
                            (struct smith_array_const_u8_n){.data = (const uint8_t *)("async"), .count = {5}},
                            SmithWrite_Replace);
    smith_files_read_async(&io->files, &read, &io->arena, "async.txt");
    smith_files_read_async(&io->files, &missing, &io->arena, "async-missing.txt");
    assert(!write.done && !read.done && !missing.done);
    size_t completed = smith_io_run(io, 0);
    assert(completed == 3 && write.done && read.done && missing.done);
    assert(write.error.code == 0 && read.error.code == 0 && strcmp((const char *)read.data.data, "async") == 0);
    assert(missing.error.code == ENOENT);
    smith_files_read_async(&io->files, &read, &io->arena, "async.txt"); // an op is reusable once done
    assert(smith_io_run(io, 0) == 1 && read.data.count[0] == 5);
}

static void test_real_console_pipe(SmithIo *io) {
    int fds[2] = {};
    assert(pipe(fds) == 0);
    const char input[] = "one\ntwo\n\nlast without newline";
    assert(write(fds[1], input, sizeof input - 1) == (ssize_t)(sizeof input - 1));
    close(fds[1]);
    int saved = dup(STDIN_FILENO);
    assert(dup2(fds[0], STDIN_FILENO) >= 0);
    close(fds[0]);
    SmithRead line = smith_console_read_line(&io->console, &io->arena);
    assert(line.error.code == 0 && !line.end && strcmp((const char *)line.data.data, "one") == 0);
    SmithOp op = {};
    smith_console_read_async(&io->console, &op, &io->arena, 4); // served from what read_line buffered
    assert(smith_io_run(io, smith_io_forever) == 1 && op.done && strcmp((const char *)op.data.data, "two\n") == 0);
    line = smith_console_read_line(&io->console, &io->arena);
    assert(!line.end && line.data.count[0] == 0);
    line = smith_console_read_line(&io->console, &io->arena);
    assert(!line.end && strcmp((const char *)line.data.data, "last without newline") == 0);
    for (size_t i = 0; i < 3; i++) { // the end stays the end
        line = smith_console_read_line(&io->console, &io->arena);
        assert(line.error.code == 0 && line.end && line.data.count[0] == 0);
    }
    smith_console_read_async(&io->console, &op, &io->arena, 16);
    assert(smith_io_run(io, smith_io_forever) == 1 && op.end && op.data.count[0] == 0);
    dup2(saved, STDIN_FILENO);
    close(saved);
}

static void test_real_processes_run(SmithIo *io) {
    const char *const echo[] = {"/bin/echo", "hello", nullptr};
    SmithProcessResult echoed = smith_processes_run(&io->processes, &io->arena, echo);
    assert(echoed.error.code == 0 && echoed.exit_status == 0 && strcmp(echoed.output, "hello\n") == 0);
    assert(echoed.errors_size == 0);

    const char *const script[] = {"/bin/sh", "-c", "echo out; echo err >&2; exit 7", nullptr};
    SmithProcessResult both = smith_processes_run(&io->processes, &io->arena, script);
    assert(both.exit_status == 7 && strcmp(both.output, "out\n") == 0 && strcmp(both.errors, "err\n") == 0);

    const char *const big[] = {"/bin/sh", "-c", "head -c 300000 /dev/zero | tr '\\0' x; echo done >&2", nullptr};
    SmithProcessResult large = smith_processes_run(&io->processes, &io->arena, big);
    assert(large.output_size == 300000 && large.output[299999] == 'x' && strcmp(large.errors, "done\n") == 0);

    const char *const killed[] = {"/bin/sh", "-c", "kill -9 $$", nullptr};
    SmithProcessResult dead = smith_processes_run(&io->processes, &io->arena, killed);
    assert(dead.signal == 9 && dead.exit_status == 0);

    const char *const nothing[] = {"/no/such/program", nullptr};
    SmithProcessResult none = smith_processes_run(&io->processes, &io->arena, nothing);
    assert(none.error.code == ENOENT && test_contains(none.error.message, "/no/such/program"));
}

// Three processes and a timeout in one loop. The quick processes end first, then the timer, then the slow one: the
// order of the events, never their durations (the slow one runs until the test ends it, so a stall of the machine
// cannot reorder them).
static void test_real_wait_several(SmithIo *io) {
    const char *const quick3[] = {"/bin/sh", "-c", "exit 3", nullptr};
    const char *const quick4[] = {"/bin/sh", "-c", "exit 4", nullptr};
    const char *const slow[] = {"/bin/sleep", "600", nullptr};
    SmithProcess processes[3] = {};
    assert(smith_processes_spawn(&io->processes, quick3, &processes[0]).code == 0);
    assert(smith_processes_spawn(&io->processes, quick4, &processes[1]).code == 0);
    assert(smith_processes_spawn(&io->processes, slow, &processes[2]).code == 0);
    SmithOp waits[3] = {}, timer = {};
    for (size_t i = 0; i < 3; i++)
        smith_processes_wait_async(&io->processes, &waits[i], processes[i]);
    smith_clock_timeout_async(&io->clock, &timer, 50 * test_millisecond);
    for (size_t round = 0; round < test_rounds_max && !(timer.done && waits[0].done && waits[1].done); round++)
        (void)smith_io_run(io, smith_io_forever);
    assert(timer.done && waits[0].exit_status == 3 && waits[1].exit_status == 4);
    assert(!waits[2].done); // the slow one is still running when the timer fires
    assert(kill(processes[2].id, SIGKILL) == 0);
    for (size_t round = 0; round < test_rounds_max && !waits[2].done; round++)
        (void)smith_io_run(io, smith_io_forever);
    assert(waits[2].done && waits[2].signal == SIGKILL && waits[2].error.code == 0);
    // A poll with nothing pending does not wait; the poll that waits for a pending timer is checked on the fake clock
    // (tests/io_test.smith), where no machine load can make the timer fire before the poll.
    assert(smith_io_run(io, 0) == 0);
    SmithOp idle = {};
    smith_clock_timeout_async(&io->clock, &idle, 10 * test_millisecond);
    assert(smith_io_run(io, smith_io_forever) == 1 && idle.done);
}

// Runs the loop until the capture is done.
static void test_capture_wait(SmithIo *io, SmithCapture *capture) {
    for (size_t round = 0; round < test_rounds_max && !smith_capture_poll(capture); round++)
        (void)smith_io_run(io, smith_io_forever);
    assert(capture->done);
}

// Output captured while the process runs: stdout and stderr in the order written, many at once on one loop, a process
// group of its own that a signal to -pid reaches whole, and the signal that killed a process.
static void test_real_capture(SmithIo *io) {
    const char *const ordered[] = {"/bin/sh", "-c", "echo one; echo two >&2; echo three; exit 3", nullptr};
    SmithCapture capture = {};
    assert(smith_processes_capture_async(&io->processes, &io->arena, ordered, &capture).code == 0);
    assert(!capture.done); // it returns at once
    test_capture_wait(io, &capture);
    assert(capture.exit_status == 3 && capture.signal == 0 && capture.error.code == 0);
    assert(capture.output_size == 14 && strcmp(capture.output, "one\ntwo\nthree\n") == 0);

    constexpr size_t many = 24;
    SmithCapture captures[24] = {};
    for (size_t i = 0; i < many; i++) {
        char script[96];
        snprintf(script, sizeof script, "sleep 0.0%zu; echo job %zu; echo error %zu >&2; exit %zu", i % 5, i, i, i);
        const char *const argv[] = {"/bin/sh", "-c", script, nullptr};
        assert(smith_processes_capture_async(&io->processes, &io->arena, argv, &captures[i]).code == 0);
    }
    for (size_t round = 0; round < test_rounds_max; round++) {
        size_t finished = 0;
        for (size_t i = 0; i < many; i++)
            finished += smith_capture_poll(&captures[i]) ? 1 : 0;
        if (finished == many) break;
        (void)smith_io_run(io, smith_io_forever);
    }
    for (size_t i = 0; i < many; i++) { // each one's output is whole, and its own
        char expected[64];
        snprintf(expected, sizeof expected, "job %zu\nerror %zu\n", i, i);
        assert(captures[i].done && captures[i].exit_status == (int)i && strcmp(captures[i].output, expected) == 0);
    }

    const char *const big[] = {"/bin/sh", "-c", "head -c 300000 /dev/zero | tr '\\0' x; echo done >&2", nullptr};
    assert(smith_processes_capture_async(&io->processes, &io->arena, big, &capture).code == 0);
    test_capture_wait(io, &capture);
    assert(capture.output_size == 300005 && capture.output[299999] == 'x' &&
           strcmp(capture.output + 300000, "done\n") == 0);

    // An arena too small for the output: the process still runs to its end, and the output is cut.
    SmithArena small = smith_arena_carve(&io->arena, (size_t)1 << 20);
    const char *const flood[] = {"/bin/sh", "-c", "head -c 5000000 /dev/zero | tr '\\0' y; exit 4", nullptr};
    assert(smith_processes_capture_async(&io->processes, &small, flood, &capture).code == 0);
    test_capture_wait(io, &capture);
    assert(capture.truncated && capture.exit_status == 4 && capture.output_size > 0);
    assert(capture.output_size < ((size_t)1 << 20) && capture.output[0] == 'y');
    assert(capture.output[capture.output_size - 1] == 'y' && capture.output[capture.output_size] == '\0');

    const char *const killed[] = {"/bin/sh", "-c", "echo before; kill -9 $$", nullptr};
    assert(smith_processes_capture_async(&io->processes, &io->arena, killed, &capture).code == 0);
    test_capture_wait(io, &capture);
    assert(capture.signal == 9 && capture.exit_status == 0 && strcmp(capture.output, "before\n") == 0);

    // The shell and the sleep it waits for are one group: one signal to the group ends both, and the output ends.
    const char *const group[] = {"/bin/sh", "-c", "sleep 30 & wait", nullptr};
    assert(smith_processes_capture_async(&io->processes, &io->arena, group, &capture).code == 0);
    assert(capture.process.id > 0 && getpgid(capture.process.id) == capture.process.id);
    (void)smith_io_run(io, 20 * test_millisecond);
    assert(!smith_capture_poll(&capture));
    assert(kill(-capture.process.id, SIGTERM) == 0);
    test_capture_wait(io, &capture);
    assert(capture.signal == SIGTERM && capture.output_size == 0);

    const char *const nothing[] = {"/no/such/program", nullptr};
    assert(smith_processes_capture_async(&io->processes, &io->arena, nothing, &capture).code == ENOENT);
}

static void test_child_until(SmithIo *io, SmithOp *op);

// A process whose id has been waited for may be any program's by now, so nothing signals it: here a bystander (a group
// of its own, as a reused id would lead) takes the id of a capture that exited and was reaped while a child of it still
// holds the output pipe, and a stop must leave the bystander alone.
static void test_real_capture_stale_stop(SmithIo *io) {
    const char *const bystander_argv[] = {"/bin/sh", "-c", "sleep 30", nullptr};
    SmithChild bystander = {};
    assert(smith_processes_start_piped(&io->processes, bystander_argv, &bystander).code == 0);
    const char *const argv[] = {"/bin/sh", "-c", "sleep 30 & exit 0", nullptr};
    SmithCapture capture = {};
    assert(smith_processes_capture_async(&io->processes, &io->arena, argv, &capture).code == 0);
    for (size_t round = 0; round < test_rounds_max && !capture.internal.wait.done; round++)
        (void)smith_io_run(io, 20 * test_millisecond);
    assert(capture.internal.wait.done && !smith_capture_poll(&capture)); // exited and reaped; the sleep holds the pipe
    int real = capture.process.id;
    capture.process.id = bystander.process.id; // the id has been reused
    assert(smith_capture_stop(&capture, SIGKILL).code == ESRCH);
    (void)smith_io_run(io, 20 * test_millisecond);
    assert(kill(bystander.process.id, 0) == 0); // still alive: nothing was sent
    capture.process.id = real;
    assert(kill(-real, SIGKILL) == 0); // the sleep of the capture's group, which the test ends itself
    test_capture_wait(io, &capture);
    assert(smith_capture_stop(&capture, SIGKILL).code == ESRCH); // done: nothing to stop
    SmithOp wait = {};
    assert(smith_child_kill(&bystander, SIGKILL).code == 0);
    smith_child_wait_async(&bystander, &wait);
    test_child_until(io, &wait);
    smith_child_close(&bystander);

    // A running capture is still stopped, with its whole group.
    const char *const running[] = {"/bin/sh", "-c", "sleep 30 & wait", nullptr};
    assert(smith_processes_capture_async(&io->processes, &io->arena, running, &capture).code == 0);
    (void)smith_io_run(io, 20 * test_millisecond);
    assert(smith_capture_stop(&capture, SIGTERM).code == 0);
    test_capture_wait(io, &capture);
    assert(capture.signal == SIGTERM);
}

// A program that talks to the terminal keeps the stdin of this process, where smith_processes_spawn gives it /dev/null.
static void test_real_terminal(SmithIo *io) {
    int pipes[2];
    assert(pipe(pipes) == 0);
    int saved = dup(STDIN_FILENO);
    dup2(pipes[0], STDIN_FILENO);
    assert(write(pipes[1], "hello\n", 6) == 6);
    const char *const reader[] = {"/bin/sh", "-c", "read line; test \"$line\" = hello", nullptr};
    SmithProcess process = {};
    assert(smith_processes_spawn_terminal(&io->processes, reader, &process).code == 0);
    SmithOp wait = {};
    smith_processes_wait_async(&io->processes, &wait, process);
    for (size_t round = 0; round < test_rounds_max && !wait.done; round++)
        (void)smith_io_run(io, smith_io_forever);
    assert(wait.done && wait.exit_status == 0 && wait.signal == 0);
    const char *const nothing[] = {"/bin/sh", "-c", "read line; test -z \"$line\"", nullptr};
    assert(smith_processes_spawn(&io->processes, nothing, &process).code == 0); // /dev/null: end of input at once
    smith_processes_wait_async(&io->processes, &wait, process);
    for (size_t round = 0; round < test_rounds_max && !wait.done; round++)
        (void)smith_io_run(io, smith_io_forever);
    assert(wait.done && wait.exit_status == 0);
    dup2(saved, STDIN_FILENO);
    close(saved);
    close(pipes[0]);
    close(pipes[1]);
}

static void test_real_clock(SmithIo *io) {
    uint64_t before = smith_clock_now(&io->clock);
    for (size_t i = 0; i < 1000; i++) {
        uint64_t now = smith_clock_now(&io->clock);
        assert(now >= before);
        before = now;
    }
    assert(smith_clock_sleep(&io->clock, 20 * test_millisecond).code == 0);
    assert(smith_clock_now(&io->clock) - before >= 20 * test_millisecond);
    int64_t wall = smith_clock_wall(&io->clock);
    int64_t seconds = (int64_t)time(nullptr);
    assert(wall / 1'000'000'000 >= seconds - 2 && wall / 1'000'000'000 <= seconds + 2);
}

static void test_seed(void) {
    setenv("SMITH_SEED", "42", 1);
    SmithIo first = smith_io_make(0, nullptr);
    SmithIo second = smith_io_make(0, nullptr);
    assert(first.seed == 42 && second.seed == 42);
    assert(smith_key_bits(first.key, 0) == smith_key_bits(smith_key_make(42), 0));
    assert(first.key.bits == second.key.bits);
    smith_io_close(&first);
    smith_io_close(&second);
    setenv("SMITH_SEED", "0xff", 1);
    SmithIo hex = smith_io_make(0, nullptr);
    assert(hex.seed == 255);
    smith_io_close(&hex);
    unsetenv("SMITH_SEED");
    SmithIo third = smith_io_make(0, nullptr);
    SmithIo fourth = smith_io_make(0, nullptr);
    assert(third.seed != fourth.seed && third.key.bits != fourth.key.bits);
    smith_io_close(&third);
    smith_io_close(&fourth);
}

static size_t test_resident(void) {
    mach_task_basic_info_data_t info = {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    kern_return_t result = task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count);
    assert(result == KERN_SUCCESS);
    (void)result;
    return info.resident_size;
}

// The reservation is address space: making the io commits (almost) nothing, touching memory commits what is touched.
static void test_memory(void) {
    size_t before = test_resident();
    SmithIo io = smith_io_make(0, nullptr);
    size_t made = test_resident();
    assert(io.arena.capacity == (size_t)64 << 30);
    assert(made - before < (size_t)16 << 20);
    constexpr size_t touched = (size_t)256 << 20;
    unsigned char *block = smith_arena_push_array(&io.arena, sizeof(unsigned char), touched, alignof(unsigned char));
    assert(block != nullptr);
    size_t after = test_resident();
    assert(after - made >= touched - ((size_t)16 << 20));
    printf("reserved %zu MiB, resident %zu KiB after make, +%zu MiB after touching %zu MiB\n", io.arena.capacity >> 20,
           (made - before) >> 10, (after - made) >> 20, touched >> 20);
    smith_io_close(&io);
}

// ---------------------------------------------------------------- a real io against a fake

static void test_same_function(SmithIo *real, SmithIo *fake) {
    const char text[] = "alpha\nbeta\ngamma\n";
    assert(
        smith_files_write(&real->files, "same.txt",
                          (struct smith_array_const_u8_n){.data = (const uint8_t *)(text), .count = {sizeof text - 1}},
                          SmithWrite_Replace)
            .code == 0);
    assert(smith_io_test_add_file(fake, "same.txt", text).code == 0);
    Summary on_disk = summarize(&real->files, &real->arena, "same.txt");
    Summary in_memory = summarize(&fake->files, &fake->arena, "same.txt");
    assert(summary_equal(&on_disk, &in_memory) && on_disk.lines == 3 && on_disk.bytes == 17);
    on_disk = summarize(&real->files, &real->arena, "absent.txt");
    in_memory = summarize(&fake->files, &fake->arena, "absent.txt");
    assert(summary_equal(&on_disk, &in_memory) && on_disk.error == ENOENT);
    assert(smith_files_write(&real->files, "../up.txt",
                             (struct smith_array_const_u8_n){.data = (const uint8_t *)("up\n"), .count = {3}},
                             SmithWrite_Replace)
               .code == 0); // a path outside the folder
    assert(smith_io_test_add_file(fake, "../up.txt", "up\n").code == 0);
    on_disk = summarize(&real->files, &real->arena, "../up.txt");
    in_memory = summarize(&fake->files, &fake->arena, "../up.txt");
    assert(summary_equal(&on_disk, &in_memory) && on_disk.lines == 1 && on_disk.error == 0);
}

// ---------------------------------------------------------------- folders, appends, maps: one function, real and fake

// Everything a program does with a folder of files. It is given files and an arena only, so it cannot tell the disk
// from the fake; `root` is a folder that does not exist yet.
static void check_tree(SmithFiles *files, SmithArena *arena, const char *root) {
    char path[256];
    snprintf(path, sizeof path, "%s/a/b", root);
    assert(smith_files_info(files, root).error.code == ENOENT);
    assert(smith_files_make_folder(files, path).code == 0); // with the folders above it
    assert(smith_files_make_folder(files, path).code == 0); // and again: it is there
    SmithFileInfo folder = smith_files_info(files, path);
    assert(folder.error.code == 0 && folder.folder && folder.writable && folder.size == 0);

    snprintf(path, sizeof path, "%s/a/one.txt", root);
    assert(smith_files_write(files, path,
                             (struct smith_array_const_u8_n){.data = (const uint8_t *)("first"), .count = {5}},
                             SmithWrite_New)
               .code == 0);
    SmithError taken = smith_files_write(
        files, path, (struct smith_array_const_u8_n){.data = (const uint8_t *)("second"), .count = {6}},
        SmithWrite_New);
    assert(taken.code == EEXIST && test_contains(taken.message, "one.txt"));
    SmithRead kept = smith_files_read(files, arena, path);
    assert(kept.error.code == 0 &&
           strcmp((const char *)kept.data.data, "first") == 0); // the second create changed nothing
    SmithFileInfo file = smith_files_info(files, path);
    assert(file.error.code == 0 && !file.folder && file.writable && file.size == 5);

    snprintf(path, sizeof path, "%s/log", root);
    assert(smith_files_write(files, path,
                             (struct smith_array_const_u8_n){.data = (const uint8_t *)("one\n"), .count = {4}},
                             SmithWrite_Append)
               .code == 0); // creates
    assert(smith_files_write(files, path,
                             (struct smith_array_const_u8_n){.data = (const uint8_t *)("two\n"), .count = {4}},
                             SmithWrite_Append)
               .code == 0);
    assert(smith_files_write(files, path, (struct smith_array_const_u8_n){}, SmithWrite_Append).code == 0);
    SmithRead log = smith_files_read(files, arena, path);
    assert(log.error.code == 0 && log.data.count[0] == 8 && strcmp((const char *)log.data.data, "one\ntwo\n") == 0);

    SmithFileList list = smith_files_list(files, arena, root); // sorted, files and folders
    assert(list.error.code == 0 && list.count == 2);
    assert(strcmp(list.entries[0].name, "a") == 0 && list.entries[0].folder && list.entries[0].size == 0);
    assert(strcmp(list.entries[1].name, "log") == 0 && !list.entries[1].folder && list.entries[1].size == 8);
    snprintf(path, sizeof path, "%s/a", root);
    list = smith_files_list(files, arena, path);
    assert(list.error.code == 0 && list.count == 2);
    assert(strcmp(list.entries[0].name, "b") == 0 && list.entries[0].folder);
    assert(strcmp(list.entries[1].name, "one.txt") == 0 && !list.entries[1].folder && list.entries[1].size == 5);
    snprintf(path, sizeof path, "%s/a/b", root);
    list = smith_files_list(files, arena, path);
    assert(list.error.code == 0 && list.count == 0); // an empty folder
    snprintf(path, sizeof path, "%s/nothing", root);
    assert(smith_files_list(files, arena, path).error.code == ENOENT);

    snprintf(path, sizeof path, "%s/a/one.txt", root);
    assert(smith_files_write(files, path,
                             (struct smith_array_const_u8_n){.data = (const uint8_t *)("replaced!"), .count = {9}},
                             SmithWrite_Atomic)
               .code == 0);
    SmithRead replaced = smith_files_read(files, arena, path);
    assert(replaced.error.code == 0 && strcmp((const char *)replaced.data.data, "replaced!") == 0);
    snprintf(path, sizeof path, "%s/a/one.txt.tmp", root);
    assert(smith_files_info(files, path).error.code == ENOENT); // the temporary file is gone
}

// The same: maps and removals, in the tree check_tree made.
static void check_map_and_remove(SmithFiles *files, SmithArena *arena, const char *root) {
    char path[256];
    snprintf(path, sizeof path, "%s/a/one.txt", root);
    SmithMapped mapped = smith_files_map(files, path);
    assert(mapped.error.code == 0 && mapped.size == 9 && memcmp(mapped.data, "replaced!", 9) == 0);
    assert((uintptr_t)mapped.data % 64 == 0);
    memcpy(mapped.data, "REPLACED?", 9); // private: the file keeps what it had
    SmithRead unchanged = smith_files_read(files, arena, path);
    assert(strcmp((const char *)unchanged.data.data, "replaced!") == 0);
    snprintf(path, sizeof path, "%s/empty", root);
    assert(smith_files_write(files, path, (struct smith_array_const_u8_n){}, SmithWrite_Replace).code == 0);
    assert(smith_files_map(files, path).error.code == EINVAL);
    snprintf(path, sizeof path, "%s/nothing", root);
    assert(smith_files_map(files, path).error.code == ENOENT);

    snprintf(path, sizeof path, "%s/a/one.txt", root);
    assert(smith_files_remove(files, path).code == 0);
    assert(smith_files_remove(files, path).code == ENOENT);
    assert(smith_files_info(files, path).error.code == ENOENT);
    snprintf(path, sizeof path, "%s/a", root);
    assert(smith_files_remove(files, path).code == EPERM); // a folder is not removed
}

static void test_real_tree(SmithIo *io) {
    char tree[64];
    snprintf(tree, sizeof tree, "tree-real-%d", getpid()); // each run in its own folder
    check_tree(&io->files, &io->arena, tree);
    check_map_and_remove(&io->files, &io->arena, tree);
    SmithFileInfo here = smith_files_info(&io->files, ".");
    assert(here.error.code == 0 && here.folder && here.writable);
    SmithFileInfo etc = smith_files_info(&io->files, "/etc/hosts");
    assert(etc.error.code == 0 && !etc.folder && etc.size > 0);
    SmithFileInfo root = smith_files_info(&io->files, "/usr");
    assert(root.error.code == 0 && root.folder && !root.writable);
    assert(smith_files_write(&io->files, "/usr/smith-io-test",
                             (struct smith_array_const_u8_n){.data = (const uint8_t *)("x"), .count = {1}},
                             SmithWrite_New)
               .code != 0);
    char blocked[96];
    snprintf(blocked, sizeof blocked, "%s/log/x", tree);
    assert(smith_files_make_folder(&io->files, blocked).code == EEXIST); // a file is in the way
}

static void test_real_map_big(SmithIo *io) {
    constexpr size_t size = (size_t)64 << 20;
    unsigned char *data = smith_arena_push_array(&io->arena, sizeof(unsigned char), size, alignof(unsigned char));
    for (size_t i = 0; i < size; i += 4096)
        data[i] = (unsigned char)(i / 4096);
    assert(smith_files_write(&io->files, "map.bin",
                             (struct smith_array_const_u8_n){.data = (const uint8_t *)(data), .count = {size}},
                             SmithWrite_Replace)
               .code == 0);
    size_t before = test_resident();
    SmithMapped mapped = smith_files_map(&io->files, "map.bin");
    size_t mapping = test_resident() - before;
    assert(mapped.error.code == 0 && mapped.size == size);
    unsigned char *bytes = mapped.data;
    assert(bytes[(size_t)4096 * 7] == 7 && bytes[size - 4096] == (unsigned char)((size - 4096) / 4096));
    size_t after = test_resident() - before;
    assert(mapping < ((size_t)1 << 20) && after < ((size_t)2 << 20)); // pages are read when touched, not before
}

static void test_real_environment_and_cores(void) {
    setenv("SMITH_IO_TEST_VARIABLE", "from the process", 1);
    SmithIo real = smith_io_make(0, nullptr);
    const char *value = smith_args_env(&real.args, "SMITH_IO_TEST_VARIABLE");
    assert(value != nullptr && strcmp(value, "from the process") == 0);
    setenv("SMITH_IO_TEST_VARIABLE", "changed after", 1);
    assert(strcmp(smith_args_env(&real.args, "SMITH_IO_TEST_VARIABLE"), "changed after") == 0);
    setenv("SMITH_IO_TEST_EMPTY", "", 1);
    assert(strcmp(smith_args_env(&real.args, "SMITH_IO_TEST_EMPTY"), "") == 0);
    unsetenv("SMITH_IO_TEST_VARIABLE");
    assert(smith_args_env(&real.args, "SMITH_IO_TEST_VARIABLE") == nullptr);
    assert(smith_processes_cores(&real.processes) >= 1);
    smith_io_close(&real);
}

// ---------------------------------------------------------------- programs run by the script

static int program_lines(void) {
    alarm(600); // a read that never ends is a hang: a blocked read spends no CPU time, so this wall backstop is large
    SmithIo io = smith_io_make(0, nullptr);
    size_t lines = 0;
    const char *last = "";
    for (size_t turn = 0; turn < test_rounds_max; turn++) {
        SmithRead line = smith_console_read_line(&io.console, &io.arena);
        if (line.end || line.error.code != 0) break;
        lines += 1;
        last = (const char *)line.data.data;
    }
    printf("%zu lines, last=%s\n", lines, last);
    return 0;
}

static int program_seed(void) {
    SmithIo io = smith_io_make(0, nullptr);
    printf("%llu %llu\n", (unsigned long long)io.seed, (unsigned long long)smith_key_bits(io.key, 0));
    return 0;
}

// ---------------------------------------------------------------- net

// A server on the loopback that answers one connection: it reads the request, waits, and sends the reply in two parts.
typedef struct {
    int listener;
    const char *first;
    const char *second;
} LoopbackServer;

static void *loopback_serve(void *argument) {
    LoopbackServer *server = argument;
    int client = accept(server->listener, nullptr, nullptr);
    char request[512];
    ssize_t n = read(client, request, sizeof request);
    (void)n;
    struct timespec pause = {.tv_nsec = 40L * 1'000'000};
    nanosleep(&pause, nullptr);
    ssize_t written = write(client, server->first, strlen(server->first));
    nanosleep(&pause, nullptr);
    written = write(client, server->second, strlen(server->second));
    (void)written;
    close(client);
    return nullptr;
}

static uint16_t loopback_listen(int *listener) {
    *listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    assert(bind(*listener, (struct sockaddr *)&address, sizeof address) == 0);
    assert(listen(*listener, 4) == 0);
    socklen_t size = sizeof address;
    assert(getsockname(*listener, (struct sockaddr *)&address, &size) == 0);
    return ntohs(address.sin_port);
}

// The real thing, against a server in this process: connections made by Network.framework, completed on the kqueue.
static void test_real_net(SmithIo *io) {
    LoopbackServer server = {.first = "HELLO ", .second = "NETWORK"};
    uint16_t port = loopback_listen(&server.listener);
    pthread_t thread = {};
    assert(pthread_create(&thread, nullptr, loopback_serve, &server) == 0);

    SmithConnection connection = {};
    SmithError error = smith_net_connect(&io->net, &connection, "127.0.0.1", port, false);
    assert(error.code == 0);
    assert(smith_net_send(
               &io->net, &connection,
               (struct smith_array_const_u8_n){.data = (const uint8_t *)("GET / HTTP/1.1\r\n\r\n"), .count = {18}})
               .code == 0);
    // The loop waits on the receive and a timer together; the data arrives in two parts.
    char buffer[64];
    SmithOp receive = {}, timer = {};
    smith_net_receive_async(&io->net, &receive, &connection,
                            (struct smith_array_u8_n){.data = (uint8_t *)(buffer), .count = {sizeof buffer - 1}});
    smith_clock_timeout_async(&io->clock, &timer, 300 * test_millisecond);
    for (size_t round = 0; round < test_rounds_max && !receive.done; round++)
        (void)smith_io_run(io, smith_io_forever);
    assert(receive.done && receive.error.code == 0 &&
           receive.data.count[0] > 0); // not that the timer is still pending: a loaded machine can stall past it
    size_t total = receive.data.count[0];
    for (size_t round = 0; round < test_rounds_max; round++) {
        SmithRead more = smith_net_receive(
            &io->net, &connection,
            (struct smith_array_u8_n){.data = (uint8_t *)(buffer + total), .count = {sizeof buffer - 1 - total}});
        assert(more.error.code == 0);
        total += more.data.count[0];
        if (more.end) break;
    }
    buffer[total] = 0;
    assert(strcmp(buffer, "HELLO NETWORK") == 0);
    assert(smith_net_close(&io->net, &connection).code == 0);
    pthread_join(thread, nullptr);
    close(server.listener);
    for (size_t round = 0; round < test_rounds_max && !timer.done; round++) // let the timer run out
        (void)smith_io_run(io, smith_io_forever);
}

static void test_real_net_errors(SmithIo *io) {
    int listener = -1;
    uint16_t port = loopback_listen(&listener);
    close(listener); // nothing listens on the port now
    SmithConnection connection = {};
    SmithError error = smith_net_connect(&io->net, &connection, "127.0.0.1", port, false);
    assert(error.code == ECONNREFUSED && test_contains(error.message, "127.0.0.1"));
    assert(smith_net_close(&io->net, &connection).code == 0);
    assert(smith_net_close(&io->net, &connection).code == 0); // closing twice is fine

    error = smith_net_connect(&io->net, &connection, "no-such-host.invalid", 443, true);
    assert(error.code == ENOENT && test_contains(error.message, "no-such-host.invalid"));
    assert(smith_net_close(&io->net, &connection).code == 0);

    // TLS to a server that does not speak it is a failure of the connect, never a hang.
    LoopbackServer server = {.first = "HTTP/1.1 200 OK\r\n\r\n", .second = "not tls"};
    port = loopback_listen(&server.listener);
    pthread_t thread = {};
    assert(pthread_create(&thread, nullptr, loopback_serve, &server) == 0);
    error = smith_net_connect(&io->net, &connection, "127.0.0.1", port, true);
    assert(error.code != 0 && error.message[0] != 0);
    assert(smith_net_close(&io->net, &connection).code == 0);
    pthread_join(thread, nullptr);
    close(server.listener);
}

// --- children with pipes

// Runs the loop until the op is done.
static void test_child_until(SmithIo *io, SmithOp *op) {
    for (size_t round = 0; round < test_rounds_max && !op->done; round++)
        (void)smith_io_run(io, smith_io_forever);
    assert(op->done);
}

// Reads from the child until total bytes have arrived (or the end), into buffer.
static size_t test_child_read(SmithIo *io, SmithChild *child, char *buffer, size_t capacity, size_t total) {
    size_t got = 0;
    SmithOp read = {};
    for (size_t round = 0; round < test_rounds_max && got < total; round++) {
        smith_child_read_async(child, &read,
                               (struct smith_array_u8_n){.data = (uint8_t *)(buffer + got), .count = {capacity - got}});
        test_child_until(io, &read);
        assert(read.error.code == 0);
        if (read.end) break;
        got += read.data.count[0];
    }
    return got;
}

static void test_child_wait(SmithIo *io, SmithChild *child, SmithOp *wait) {
    smith_processes_wait_async(&io->processes, wait, child->process);
    test_child_until(io, wait);
}

// cat echoes what is written; closing its stdin ends it.
static void test_real_child_cat(SmithIo *io) {
    const char *const argv[] = {"/bin/cat", nullptr};
    SmithChild child = {};
    assert(smith_processes_start_piped(&io->processes, argv, &child).code == 0);
    assert(child.process.id > 0 && getpgid(child.process.id) == child.process.id);
    SmithOp write = {};
    smith_child_write_async(&child, &write,
                            (struct smith_array_const_u8_n){.data = (const uint8_t *)("hello\n"), .count = {6}});
    test_child_until(io, &write);
    assert(write.error.code == 0 && write.taken == 6);
    char buffer[64];
    assert(test_child_read(io, &child, buffer, sizeof buffer, 6) == 6 && memcmp(buffer, "hello\n", 6) == 0);
    smith_child_close_input(&child);
    assert(test_child_read(io, &child, buffer, sizeof buffer, 1) == 0); // the end
    SmithOp wait = {};
    test_child_wait(io, &child, &wait);
    assert(wait.exit_status == 0 && wait.signal == 0);
    smith_child_close(&child);

    const char *const nothing[] = {"/no/such/program", nullptr};
    assert(smith_processes_start_piped(&io->processes, nothing, &child).code == ENOENT);
}

// A child that has exited: its output can still be read, and a write is EPIPE, not a signal that kills the program.
static void test_real_child_early_exit(SmithIo *io) {
    const char *const argv[] = {"/bin/sh", "-c", "echo bye; exit 3", nullptr};
    SmithChild child = {};
    assert(smith_processes_start_piped(&io->processes, argv, &child).code == 0);
    SmithOp wait = {};
    test_child_wait(io, &child, &wait);
    assert(wait.exit_status == 3);
    for (size_t i = 0; i < 3; i++) { // every write, not only the first
        SmithOp write = {};
        smith_child_write_async(&child, &write,
                                (struct smith_array_const_u8_n){.data = (const uint8_t *)("late\n"), .count = {5}});
        test_child_until(io, &write);
        assert(write.error.code == EPIPE && write.taken == 0);
    }
    char buffer[16];
    assert(test_child_read(io, &child, buffer, sizeof buffer, 4) == 4 && memcmp(buffer, "bye\n", 4) == 0);
    smith_child_close(&child);
}

// Megabytes through cat, written and read on one loop: the pipes hold 64 KiB, so a program that wrote it all before it
// read would deadlock; with both pending at once it cannot. The bytes come back whole and in order.
static void test_real_child_large(SmithIo *io) {
    constexpr size_t total = (size_t)4 << 20;
    constexpr size_t block = (size_t)64 << 10;
    unsigned char *input = smith_arena_push(&io->arena, total, 1);
    for (size_t i = 0; i < total; i++)
        input[i] = (unsigned char)(i * 131 + (i >> 8));
    unsigned char *output = smith_arena_push(&io->arena, total + 1, 1);
    const char *const argv[] = {"/bin/cat", nullptr};
    SmithChild child = {};
    assert(smith_processes_start_piped(&io->processes, argv, &child).code == 0);
    SmithOp write = {};
    SmithOp read = {};
    smith_child_write_async(&child, &write,
                            (struct smith_array_const_u8_n){.data = (const uint8_t *)(input), .count = {total}});
    smith_child_read_async(&child, &read, (struct smith_array_u8_n){.data = (uint8_t *)(output), .count = {block}});
    size_t got = 0;
    bool closed = false;
    for (size_t round = 0; round < test_rounds_max && !(closed && read.done && read.end); round++) {
        if (write.done && !closed) {
            assert(write.error.code == 0 && write.taken == total);
            smith_child_close_input(&child);
            closed = true;
        }
        if (read.done && !read.end) {
            assert(read.error.code == 0);
            got += read.data.count[0];
            smith_child_read_async(
                &child, &read,
                (struct smith_array_u8_n){.data = (uint8_t *)(output + got),
                                          .count = {total + 1 - got < block ? total + 1 - got : block}});
        }
        if (!(closed && read.done && read.end)) (void)smith_io_run(io, smith_io_forever);
    }
    assert(closed && got == total && memcmp(input, output, total) == 0);
    SmithOp wait = {};
    test_child_wait(io, &child, &wait);
    assert(wait.exit_status == 0);
    smith_child_close(&child);
}

// Two children on one loop, each answering to its own writes.
static void test_real_child_two(SmithIo *io) {
    const char *const argv[] = {"/bin/cat", nullptr};
    SmithChild children[2] = {};
    SmithOp writes[2] = {};
    char buffers[2][16] = {};
    for (size_t i = 0; i < 2; i++)
        assert(smith_processes_start_piped(&io->processes, argv, &children[i]).code == 0);
    assert(children[0].process.id != children[1].process.id);
    smith_child_write_async(&children[0], &writes[0],
                            (struct smith_array_const_u8_n){.data = (const uint8_t *)("first\n"), .count = {6}});
    smith_child_write_async(&children[1], &writes[1],
                            (struct smith_array_const_u8_n){.data = (const uint8_t *)("second\n"), .count = {7}});
    assert(test_child_read(io, &children[1], buffers[1], sizeof buffers[1], 7) == 7);
    assert(test_child_read(io, &children[0], buffers[0], sizeof buffers[0], 6) == 6);
    assert(memcmp(buffers[0], "first\n", 6) == 0 && memcmp(buffers[1], "second\n", 7) == 0);
    assert(writes[0].done && writes[1].done);
    for (size_t i = 0; i < 2; i++) {
        smith_child_close_input(&children[i]);
        SmithOp wait = {};
        test_child_wait(io, &children[i], &wait);
        assert(wait.exit_status == 0);
        smith_child_close(&children[i]);
    }
}

// A child's stderr is the program's own, so a flood there never blocks it (here the program's stderr is /dev/null).
static void test_real_child_stderr(SmithIo *io) {
    int saved = dup(STDERR_FILENO);
    int null = open("/dev/null", O_WRONLY);
    dup2(null, STDERR_FILENO);
    const char *const argv[] = {"/bin/sh", "-c", "head -c 2000000 /dev/zero >&2; echo ok", nullptr};
    SmithChild child = {};
    SmithError error = smith_processes_start_piped(&io->processes, argv, &child);
    dup2(saved, STDERR_FILENO);
    close(saved);
    close(null);
    assert(error.code == 0);
    char buffer[8];
    assert(test_child_read(io, &child, buffer, sizeof buffer, 3) == 3 && memcmp(buffer, "ok\n", 3) == 0);
    SmithOp wait = {};
    test_child_wait(io, &child, &wait);
    assert(wait.exit_status == 0);
    smith_child_close(&child);
}

// Killing reaches the whole group: the shell and the sleep it waits for.
static void test_real_child_kill(SmithIo *io) {
    const char *const argv[] = {"/bin/sh", "-c", "sleep 30 & wait", nullptr};
    SmithChild child = {};
    assert(smith_processes_start_piped(&io->processes, argv, &child).code == 0);
    SmithOp wait = {};
    SmithOp read = {};
    char buffer[8];
    smith_processes_wait_async(&io->processes, &wait, child.process);
    smith_child_read_async(&child, &read,
                           (struct smith_array_u8_n){.data = (uint8_t *)(buffer), .count = {sizeof buffer}});
    (void)smith_io_run(io, 20 * test_millisecond);
    assert(!wait.done && !read.done);
    assert(smith_child_kill(&child, SIGTERM).code == 0);
    test_child_until(io, &wait);
    assert(wait.signal == SIGTERM && wait.exit_status == 0);
    test_child_until(io, &read); // the sleep died with its group, so the pipe is closed
    assert(read.end);
    smith_child_close(&child);
}

// A kill after the wait has completed signals nothing: the id may be another program's. The bystander takes the exited
// child's id, as a reused one would, and survives the kill.
static void test_real_child_kill_after_wait(SmithIo *io) {
    const char *const bystander_argv[] = {"/bin/sh", "-c", "sleep 30", nullptr};
    SmithChild bystander = {};
    assert(smith_processes_start_piped(&io->processes, bystander_argv, &bystander).code == 0);
    const char *const quick_argv[] = {"/bin/sh", "-c", "exit 0", nullptr};
    SmithChild quick = {};
    assert(smith_processes_start_piped(&io->processes, quick_argv, &quick).code == 0);
    SmithOp wait = {};
    smith_child_wait_async(&quick, &wait);
    test_child_until(io, &wait);
    assert(wait.exit_status == 0);
    quick.process.id = bystander.process.id; // the id has been reused
    assert(smith_child_kill(&quick, SIGKILL).code == ESRCH);
    (void)smith_io_run(io, 20 * test_millisecond);
    assert(kill(bystander.process.id, 0) == 0); // nothing was sent
    smith_child_close(&quick);
    SmithOp bystander_wait = {};
    smith_child_wait_async(&bystander, &bystander_wait);
    assert(smith_child_kill(&bystander, SIGKILL).code == 0); // its wait has not completed: it is signalled
    test_child_until(io, &bystander_wait);
    assert(bystander_wait.signal == SIGKILL);
    assert(smith_child_kill(&bystander, SIGKILL).code == ESRCH);
    smith_child_close(&bystander);
}

static void test_real_children(SmithIo *io) {
    test_real_child_cat(io);
    test_real_child_early_exit(io);
    test_real_child_large(io);
    test_real_child_two(io);
    test_real_child_stderr(io);
    test_real_child_kill(io);
    test_real_child_kill_after_wait(io);
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "lines") == 0) return program_lines();
    if (argc > 1 && strcmp(argv[1], "seed") == 0) return program_seed();
    if (argc > 1 && strcmp(argv[1], "memory") == 0) {
        test_memory();
        return 0;
    }
    SmithIo io = smith_io_make(argc, argv);
    assert(io.args.count == (size_t)argc && strcmp(smith_args_get(&io.args, 0), argv[0]) == 0);
    test_real_files(&io);
    test_real_big_file(&io);
    test_real_async_files(&io);
    test_real_processes_run(&io);
    test_real_wait_several(&io);
    test_real_capture(&io);
    test_real_capture_stale_stop(&io);
    test_real_terminal(&io);
    test_real_children(&io);
    test_real_clock(&io);
    test_real_console_pipe(&io);
    test_real_tree(&io);
    test_real_map_big(&io);
    test_real_net(&io);
    test_real_net_errors(&io);
    static TestMemory same_memory;
    SmithIoTest config = {.memory = same_memory.bytes, .capacity = sizeof same_memory.bytes};
    SmithIo fake = smith_io_make_test(config);
    test_same_function(&io, &fake);
    test_real_environment_and_cores();
    test_seed();
    smith_io_close(&io);
    puts("io: all checks passed");
    return 0;
}
