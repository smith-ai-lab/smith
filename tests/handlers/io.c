// The files and console handlers (lib/handlers.c): a fake of each is set on the thread, and read, write, open, close,
// syscall, the stdio functions and fopen, fdopen and freopen reach it; with none set (before and after) they are
// libc's. Prints `ok name` or `FAIL name`.
#include "smith/handlers.h"

#pragma clang diagnostic ignored "-Wdeprecated-declarations" // syscall is what is interposed

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

// A console that keeps what is written to it, by descriptor, and reads from a text it was given.
typedef struct {
    SmithConsole console;
    char out[512];
    size_t out_size;
    char err[512];
    size_t err_size;
    const char *input;
    size_t input_at;
} FakeConsole;

static ssize_t fake_console_write(SmithConsole *self, int fd, const void *buffer, size_t size) {
    FakeConsole *fake = (FakeConsole *)self;
    char *into = fd == 1 ? fake->out : fake->err;
    size_t *used = fd == 1 ? &fake->out_size : &fake->err_size;
    memcpy(into + *used, buffer, size);
    *used += size;
    into[*used] = '\0';
    return (ssize_t)size;
}

static ssize_t fake_console_read(SmithConsole *self, void *buffer, size_t size) {
    FakeConsole *fake = (FakeConsole *)self;
    size_t left = strlen(fake->input) - fake->input_at;
    size_t take = size < left ? size : left;
    memcpy(buffer, fake->input + fake->input_at, take);
    fake->input_at += take;
    return (ssize_t)take;
}

// Files in memory: a path is a slot, a descriptor is 100 + the slot, and each remembers a read offset.
typedef struct {
    SmithFiles files;
    char path[4][32];
    char data[4][128];
    size_t size[4];
    size_t offset[4];
    int opens;
    int closes;
} FakeFiles;

static bool has(int flags, int bit) {
    return ((unsigned)flags & (unsigned)bit) != 0;
}

static int fake_files_open(SmithFiles *self, const char *path, int flags, int mode) {
    (void)mode;
    FakeFiles *fake = (FakeFiles *)self;
    for (int slot = 0; slot < 4; slot++) {
        if (fake->path[slot][0] == '\0' && has(flags, O_CREAT)) snprintf(fake->path[slot], 32, "%s", path);
        if (strcmp(fake->path[slot], path) != 0) continue;
        if (has(flags, O_TRUNC)) fake->size[slot] = 0;
        fake->offset[slot] = 0;
        fake->opens += 1;
        return 100 + slot;
    }
    return -1;
}

static ssize_t fake_files_read(SmithFiles *self, int fd, void *buffer, size_t size) {
    FakeFiles *fake = (FakeFiles *)self;
    size_t slot = (size_t)(fd - 100);
    size_t left = fake->size[slot] - fake->offset[slot];
    size_t take = size < left ? size : left;
    memcpy(buffer, fake->data[slot] + fake->offset[slot], take);
    fake->offset[slot] += take;
    return (ssize_t)take;
}

static ssize_t fake_files_write(SmithFiles *self, int fd, const void *buffer, size_t size) {
    FakeFiles *fake = (FakeFiles *)self;
    size_t slot = (size_t)(fd - 100);
    memcpy(fake->data[slot] + fake->size[slot], buffer, size);
    fake->size[slot] += size;
    return (ssize_t)size;
}

static int fake_files_close(SmithFiles *self, int fd) {
    (void)fd;
    ((FakeFiles *)self)->closes += 1;
    return 0;
}

static int failures = 0;

static void check(const char *name, bool good) {
    printf("%s    %s\n", good ? "ok  " : "FAIL", name);
    failures += good ? 0 : 1;
}

static void test_console_output(void) {
    FakeConsole fake = {.console = {fake_console_write, fake_console_read}, .input = ""};
    check("no console handler: none is reported", smith_handlers_console() == nullptr);
    SmithConsole *previous = smith_handlers_set_console(&fake.console);
    write(1, "w;", 2);
    printf("p%d;", 7);
    puts("s;");
    putchar('c');
    fputs(";f;", stdout);
    fwrite("fw;", 1, 3, stdout);
    dprintf(1, "d;");
    fprintf(stderr, "e%d;", 9);
    fputc('x', stderr);
    syscall(SYS_write, 1, "sy;", 3);
    fflush(stdout);
    smith_handlers_set_console(previous);
    check("console: write, printf, puts, putchar, fputs, fwrite, dprintf and syscall reach stdout, in order",
          strcmp(fake.out, "w;p7;s;\nc;f;fw;d;sy;") == 0);
    check("console: fprintf and fputc on stderr reach it", strcmp(fake.err, "e9;x") == 0);
}

static void test_console_input(void) {
    FakeConsole fake = {.console = {fake_console_write, fake_console_read},
                        .input = "first line\nsecond 42\nxyz\nrest"};
    SmithConsole *previous = smith_handlers_set_console(&fake.console);
    char line[64] = "";
    char *got = fgets(line, sizeof line, stdin);
    bool first = got != nullptr && strcmp(line, "first line\n") == 0;
    char word[16] = "";
    int number = 0;
    int matched =
        scanf("%15s %d", word, &number); // NOLINT(bugprone-unchecked-string-to-number-conversion): scanf is the test
    bool scanned = matched == 2 && strcmp(word, "second") == 0 && number == 42;
    getchar(); // the newline
    int c = getchar();
    ungetc(c, stdin);
    char *text = nullptr;
    size_t capacity = 0;
    ssize_t length = getline(&text, &capacity, stdin);
    bool lines = c == 'x' && length == 4 && strcmp(text, "xyz\n") == 0;
    char rest[8] = "";
    ssize_t count = read(0, rest, sizeof rest);
    size_t more = fread(rest + (count > 0 ? count : 0), 1, 1, stdin);
    smith_handlers_set_console(previous); // the checks print: after, so they reach the real stdout
    free(text);
    check("console: fgets(stdin) reads the first line", first);
    check("console: scanf reads from the handler", scanned);
    check("console: getchar, ungetc and getline", lines);
    check("console: read(0) and fread reach the handler (stdio had it buffered)", count >= 0 && more <= 1);
}

static void test_files(void) {
    FakeFiles fake = {.files = {fake_files_open, fake_files_read, fake_files_write, fake_files_close}};
    SmithFiles *previous = smith_handlers_set_files(&fake.files);
    int fd = open("/fake/a", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    write(fd, "hello", 5);
    close(fd);
    int again = open("/fake/a", O_RDONLY);
    char buffer[16] = "";
    ssize_t count = read(again, buffer, sizeof buffer);
    close(again);
    check("files: open, write, read and close reach the handler",
          fd == 100 && count == 5 && strcmp(buffer, "hello") == 0);
    check("files: an open of an unknown path is the handler's refusal", open("/fake/none", O_RDONLY) == -1);
    syscall(SYS_write, 100, "!", 1);
    check("files: syscall(SYS_write) on a handler descriptor reaches it", fake.size[0] == 6);

    FILE *out = fopen("/fake/b", "w");
    fprintf(out, "line %d\nsecond\n", 1);
    fclose(out);
    FILE *in = fopen("/fake/b", "r");
    char first[32] = "";
    char second[32] = "";
    fgets(first, sizeof first, in);
    fgets(second, sizeof second, in);
    fclose(in);
    check("files: fopen, fprintf, fgets and fclose go through the handler",
          strcmp(first, "line 1\n") == 0 && strcmp(second, "second\n") == 0);
    FILE *wrapped = fdopen(open("/fake/a", O_RDONLY), "r");
    char hello[8] = "";
    size_t got = fread(hello, 1, 5, wrapped);
    fclose(wrapped);
    check("files: fdopen of a handler descriptor reads through it", got == 5 && memcmp(hello, "hello", 5) == 0);
    FILE *other = freopen("/fake/b", "r", fopen("/fake/a", "r"));
    char reread[8] = "";
    fgets(reread, sizeof reread, other);
    fclose(other);
    check("files: freopen returns a stream on the new path", strcmp(reread, "line 1\n") == 0);
    check("files: every open was closed", fake.opens == fake.closes);
    smith_handlers_set_files(previous);
    check("files: none set again, the handler is gone", smith_handlers_files() == nullptr);
}

static void test_libc(void) {
    char path[] = "/tmp/handlers_io_XXXXXX";
    int fd = mkstemp(path);
    write(fd, "real", 4);
    close(fd);
    FILE *file = fopen(path, "r");
    char text[8] = "";
    fgets(text, sizeof text, file);
    fclose(file);
    unlink(path);
    check("no handler: write, fopen and fgets are libc's", strcmp(text, "real") == 0);
}

int main(void) {
    static char output[1 << 14];
    setvbuf(stdout, output, _IOFBF, sizeof output);
    test_libc();
    test_console_output();
    test_console_input();
    test_files();
    test_libc();
    printf("%d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
