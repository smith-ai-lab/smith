// A console handler catches printf, puts and fputs in a program built by the conventions, in debug (ASan, which checks
// at start-up that puts is its own: the build turns that check off, build_runtime.c) and in release. Exits 0 when it
// does.
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "smith/handlers.h"

typedef struct {
    SmithConsoleHandler console;
    char out[256];
    size_t n;
} Fake;
static ssize_t fw(SmithConsoleHandler *s, int fd, const void *b, size_t size) {
    Fake *f = (Fake *)s;
    if (fd == 1) {
        memcpy(f->out + f->n, b, size);
        f->n += size;
        f->out[f->n] = 0;
    }
    return (ssize_t)size;
}
static ssize_t fr(SmithConsoleHandler *s, void *b, size_t size) {
    (void)s;
    (void)b;
    (void)size;
    return 0;
}
int main(void) {
    Fake f = {.console = {fw, fr}};
    SmithConsoleHandler *prev = smith_handlers_set_console(&f.console);
    printf("x\n");
    puts("y");
    fputs("z\n", stdout);
    smith_handlers_set_console(prev);
    int ok = strcmp(f.out, "x\ny\nz\n") == 0;
    fprintf(stderr, "%s captured=[%s]\n", ok ? "ok" : "FAIL", f.out);
    return ok ? 0 : 1;
}
