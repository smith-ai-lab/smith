// Tests of <smith/io.h>, run by tests/io.sh in a scratch folder: with no argument, the checks below; with an argument,
// one program that reads the world and prints what it saw:
//   lines   reads stdin to the end with smith_console_read_line and prints "N lines, last=..."
//   seed    prints the seed and the first draw of the key
//   memory  prints how much of the arena's reservation counts as resident before and after it is touched
#include <smith/io.h>

#include <assert.h>
#include <errno.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    summary.bytes = text.size;
    for (size_t i = 0; i < text.size; i++)
        if (text.data[i] == '\n') summary.lines += 1;
    return summary;
}

static bool summary_equal(const Summary *a, const Summary *b) {
    return a->lines == b->lines && a->bytes == b->bytes && a->error == b->error && strcmp(a->message, b->message) == 0;
}

// Checkers' loop: it reads moves until the input ends. The turn bound stops a runaway; the end of the input ends it.
static size_t play(SmithConsole *console, SmithArena *arena, size_t turn_max) {
    size_t turns = 0;
    for (; turns < turn_max; turns++) {
        SmithRead line = smith_console_read_line(console, arena);
        if (line.error.code != 0 || line.end) break;
        SmithError error = smith_console_print(console, "move: ");
        assert(error.code == 0);
        error = smith_console_print(console, line.data);
        assert(error.code == 0);
        error = smith_console_print(console, "\n");
        assert(error.code == 0);
    }
    return turns;
}

// ---------------------------------------------------------------- real io

static void test_real_files(SmithIo *io) {
    const char text[] = "hello\nworld\n";
    SmithError error = smith_files_write(&io->files, "hello.txt", text, sizeof text - 1);
    assert(error.code == 0);
    SmithRead read = smith_files_read(&io->files, &io->arena, "hello.txt");
    assert(read.error.code == 0 && read.size == sizeof text - 1 && strcmp(read.data, text) == 0);

    SmithRead missing = smith_files_read(&io->files, &io->arena, "missing.txt");
    assert(missing.error.code == ENOENT);
    assert(strcmp(missing.error.message, "open missing.txt: No such file or directory") == 0);
    assert(missing.data == nullptr && missing.size == 0);

    assert(smith_files_read(&io->files, &io->arena, "/etc/hosts").error.code == EINVAL);
    assert(smith_files_read(&io->files, &io->arena, "../hello.txt").error.code == EINVAL);
    assert(smith_files_read(&io->files, &io->arena, "a/../../x").error.code == EINVAL);
    assert(smith_files_read(&io->files, &io->arena, "a..b").error.code == ENOENT); // not a ".." component
    assert(smith_files_write(&io->files, "no/such/folder/x", text, 1).code == ENOENT);
}

static void test_real_big_file(SmithIo *io) {
    constexpr size_t size = (size_t)3 << 20;
    unsigned char *data = SMITH_ARENA_PUSH_ARRAY(&io->arena, unsigned char, size);
    for (size_t i = 0; i < size; i++)
        data[i] = (unsigned char)(i * 7 + 1);
    assert(smith_files_write(&io->files, "big.bin", data, size).code == 0);
    SmithRead read = smith_files_read(&io->files, &io->arena, "big.bin");
    assert(read.error.code == 0 && read.size == size && memcmp(read.data, data, size) == 0);
}

// Several file operations in one loop: they run in the order submitted, so the read sees the write before it.
static void test_real_async_files(SmithIo *io) {
    SmithOp write = {}, read = {}, missing = {};
    smith_files_write_async(&io->files, &write, "async.txt", "async", 5);
    smith_files_read_async(&io->files, &read, &io->arena, "async.txt");
    smith_files_read_async(&io->files, &missing, &io->arena, "async-missing.txt");
    assert(!write.done && !read.done && !missing.done);
    size_t completed = smith_io_run(io, 0);
    assert(completed == 3 && write.done && read.done && missing.done);
    assert(write.error.code == 0 && read.error.code == 0 && strcmp(read.data, "async") == 0);
    assert(missing.error.code == ENOENT);
    smith_files_read_async(&io->files, &read, &io->arena, "async.txt"); // an op is reusable once done
    assert(smith_io_run(io, 0) == 1 && read.size == 5);
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
    assert(line.error.code == 0 && !line.end && strcmp(line.data, "one") == 0);
    SmithOp op = {};
    smith_console_read_async(&io->console, &op, &io->arena, 4); // served from what read_line buffered
    assert(smith_io_run(io, smith_io_forever) == 1 && op.done && strcmp(op.data, "two\n") == 0);
    line = smith_console_read_line(&io->console, &io->arena);
    assert(!line.end && line.size == 0);
    line = smith_console_read_line(&io->console, &io->arena);
    assert(!line.end && strcmp(line.data, "last without newline") == 0);
    for (size_t i = 0; i < 3; i++) { // the end stays the end
        line = smith_console_read_line(&io->console, &io->arena);
        assert(line.error.code == 0 && line.end && line.size == 0);
    }
    smith_console_read_async(&io->console, &op, &io->arena, 16);
    assert(smith_io_run(io, smith_io_forever) == 1 && op.end && op.size == 0);
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

// Three processes and a timeout in one loop. The timer (50 ms) fires after the quick processes and before the slow one.
static void test_real_wait_several(SmithIo *io) {
    const char *const quick3[] = {"/bin/sh", "-c", "exit 3", nullptr};
    const char *const quick4[] = {"/bin/sh", "-c", "exit 4", nullptr};
    const char *const slow[] = {"/bin/sleep", "0.4", nullptr};
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
    for (size_t round = 0; round < test_rounds_max && !waits[2].done; round++)
        (void)smith_io_run(io, smith_io_forever);
    assert(waits[2].done && waits[2].exit_status == 0 && waits[2].error.code == 0);
    SmithOp idle = {};
    smith_clock_timeout_async(&io->clock, &idle, 10 * test_millisecond);
    assert(smith_io_run(io, 0) == 0 && !idle.done); // a poll does not wait
    assert(smith_io_run(io, smith_io_forever) == 1 && idle.done);
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
#if __has_feature(address_sanitizer)
    puts("skipped: AddressSanitizer commits shadow memory for the arena, so the library reserves 1 GiB under it");
    return;
#endif
    size_t before = test_resident();
    SmithIo io = smith_io_make(0, nullptr);
    size_t made = test_resident();
    assert(io.arena.capacity >= (size_t)1 << 30);
    assert(made - before < (size_t)16 << 20);
    constexpr size_t touched = (size_t)256 << 20;
    unsigned char *block = SMITH_ARENA_PUSH_ARRAY(&io.arena, unsigned char, touched);
    assert(block != nullptr);
    size_t after = test_resident();
    assert(after - made >= touched - ((size_t)16 << 20));
    printf("reserved %zu MiB, resident %zu KiB after make, +%zu MiB after touching %zu MiB\n", io.arena.capacity >> 20,
           (made - before) >> 10, (after - made) >> 20, touched >> 20);
    smith_io_close(&io);
}

// ---------------------------------------------------------------- the fake io

static void test_same_function(SmithIo *real, SmithIo *fake) {
    const char text[] = "alpha\nbeta\ngamma\n";
    assert(smith_files_write(&real->files, "same.txt", text, sizeof text - 1).code == 0);
    assert(smith_io_test_add_file(fake, "same.txt", text).code == 0);
    Summary on_disk = summarize(&real->files, &real->arena, "same.txt");
    Summary in_memory = summarize(&fake->files, &fake->arena, "same.txt");
    assert(summary_equal(&on_disk, &in_memory) && on_disk.lines == 3 && on_disk.bytes == 17);
    on_disk = summarize(&real->files, &real->arena, "absent.txt");
    in_memory = summarize(&fake->files, &fake->arena, "absent.txt");
    assert(summary_equal(&on_disk, &in_memory) && on_disk.error == ENOENT);
    on_disk = summarize(&real->files, &real->arena, "/etc/hosts");
    in_memory = summarize(&fake->files, &fake->arena, "/etc/hosts");
    assert(summary_equal(&on_disk, &in_memory) && on_disk.error == EINVAL);

    assert(smith_files_write(&fake->files, "new.txt", "xyz", 3).code == 0); // writes land in memory
    SmithRead back = smith_files_read(&fake->files, &fake->arena, "new.txt");
    assert(back.size == 3 && strcmp(back.data, "xyz") == 0);
    assert(smith_files_write(&fake->files, "new.txt", "q", 1).code == 0); // and replace
    assert(smith_files_read(&fake->files, &fake->arena, "new.txt").size == 1);
}

static void test_fake_console(TestMemory *memory) {
    SmithIoTest config = {
        .memory = memory->bytes, .capacity = sizeof memory->bytes, .seed = 7, .input = "A3 B4\nC5 D6\nlast"};
    SmithIo io = smith_io_make_test(config);
    // Without the end check this loop would run to its bound; with it, it ends after the three lines the script holds.
    assert(play(&io.console, &io.arena, 1000) == 3);
    assert(strcmp(smith_io_test_output(&io), "move: A3 B4\nmove: C5 D6\nmove: last\n") == 0);
    assert(strcmp(smith_io_test_errors(&io), "") == 0);
    assert(smith_console_print_error(&io.console, "oops").code == 0);
    assert(strcmp(smith_io_test_errors(&io), "oops") == 0);

    static TestMemory quiet_memory;
    SmithIoTest none = {.memory = quiet_memory.bytes, .capacity = sizeof quiet_memory.bytes};
    SmithIo silent = smith_io_make_test(none);
    assert(play(&silent.console, &silent.arena, 1000) == 0); // no input: the loop ends at once
    SmithOp op = {};
    smith_console_read_async(&io.console, &op, &io.arena, 8);
    assert(smith_io_run(&io, 0) == 1 && op.end && op.size == 0); // everything was consumed
}

static void test_fake_clock(TestMemory *memory) {
    SmithIoTest config = {.memory = memory->bytes, .capacity = sizeof memory->bytes};
    SmithIo io = smith_io_make_test(config);
    uint64_t start = smith_clock_now(&io.clock);
    assert(smith_clock_now(&io.clock) == start); // it moves only when told to
    assert(smith_clock_wall(&io.clock) == smith_clock_wall(&io.clock));
    SmithOp timer = {};
    smith_clock_timeout_async(&io.clock, &timer, 100);
    assert(smith_io_run(&io, smith_io_forever) == 0 && !timer.done); // the fake never waits
    smith_clock_advance(&io.clock, 99);
    assert(smith_io_run(&io, 0) == 0 && !timer.done);
    smith_clock_advance(&io.clock, 1);
    assert(smith_clock_now(&io.clock) == start + 100);
    assert(smith_io_run(&io, 0) == 1 && timer.done && timer.error.code == 0);
    assert(smith_clock_sleep(&io.clock, 0).code == 0);
    assert(smith_clock_sleep(&io.clock, 5).code == EDEADLK); // nothing could ever wake it
}

static void test_fake_rest(TestMemory *memory) {
    const char *const argv[] = {"prog", "--flag", nullptr};
    SmithIoTest config = {
        .memory = memory->bytes, .capacity = sizeof memory->bytes, .seed = 99, .argv = argv, .argc = 2};
    SmithIo io = smith_io_make_test(config);
    assert(io.seed == 99 && io.key.bits == smith_key_make(99).bits);
    assert(io.args.count == 2 && strcmp(smith_args_get(&io.args, 1), "--flag") == 0);
    assert(smith_args_get(&io.args, 2) == nullptr);
    assert(smith_io_test_add_program(&io, "tool", "tool output\n", 5).code == 0);
    const char *const run[] = {"tool", nullptr};
    SmithProcessResult result = smith_processes_run(&io.processes, &io.arena, run);
    assert(result.exit_status == 5 && strcmp(result.output, "tool output\n") == 0);
    SmithProcess process = {};
    assert(smith_processes_spawn(&io.processes, run, &process).code == 0);
    SmithOp wait = {};
    smith_processes_wait_async(&io.processes, &wait, process);
    assert(smith_io_run(&io, 0) == 1 && wait.exit_status == 5);
    const char *const absent[] = {"absent", nullptr};
    assert(smith_processes_run(&io.processes, &io.arena, absent).error.code == ENOENT);
}

// ---------------------------------------------------------------- programs run by the script

static int program_lines(void) {
    SmithIo io = smith_io_make(0, nullptr);
    size_t lines = 0;
    const char *last = "";
    for (size_t turn = 0; turn < test_rounds_max; turn++) {
        SmithRead line = smith_console_read_line(&io.console, &io.arena);
        if (line.end || line.error.code != 0) break;
        lines += 1;
        last = line.data;
    }
    printf("%zu lines, last=%s\n", lines, last);
    return 0;
}

static int program_seed(void) {
    SmithIo io = smith_io_make(0, nullptr);
    printf("%llu %llu\n", (unsigned long long)io.seed, (unsigned long long)smith_key_bits(io.key, 0));
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "lines") == 0) return program_lines();
    if (argc > 1 && strcmp(argv[1], "seed") == 0) return program_seed();
    if (argc > 1 && strcmp(argv[1], "memory") == 0) {
        test_memory();
        return 0;
    }
    static TestMemory fake_memory;
    SmithIo io = smith_io_make(argc, argv);
    assert(io.args.count == (size_t)argc && strcmp(smith_args_get(&io.args, 0), argv[0]) == 0);
    test_real_files(&io);
    test_real_big_file(&io);
    test_real_async_files(&io);
    test_real_processes_run(&io);
    test_real_wait_several(&io);
    test_real_clock(&io);
    test_real_console_pipe(&io);
    static TestMemory same_memory;
    SmithIoTest config = {.memory = same_memory.bytes, .capacity = sizeof same_memory.bytes};
    SmithIo fake = smith_io_make_test(config);
    test_same_function(&io, &fake);
    test_fake_console(&fake_memory);
    test_fake_clock(&fake_memory);
    test_fake_rest(&fake_memory);
    test_seed();
    smith_io_close(&io);
    puts("io: all checks passed");
    return 0;
}
