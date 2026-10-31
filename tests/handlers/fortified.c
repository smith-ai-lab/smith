// A console handler catches printf, fprintf and dprintf in a program built with _FORTIFY_SOURCE (Linux), which calls
// __printf_chk, __fprintf_chk and __dprintf_chk instead; a number is printed so the optimizer keeps them. Exits 0 when
// it does.
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
    volatile int n = 7;
    printf("x%d\n", n);
    fprintf(stdout, "y%d\n", n);
    dprintf(1, "z%d\n", n);
    smith_handlers_set_console(prev);
    int ok = strcmp(f.out, "x7\ny7\nz7\n") == 0;
    fprintf(stderr, "%s captured=[%s]\n", ok ? "ok" : "FAIL", f.out);
    return ok ? 0 : 1;
}
