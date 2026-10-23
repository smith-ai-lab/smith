// Ordinary C, one construct after another: it behaves as it does in a .c file.
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

enum color { red = 1, green = 2, blue = 4 };
union word {
    int i;
    unsigned char b[sizeof(int)];
};
struct bits {
    unsigned a : 3, b : 5;
    int c;
};

static int one(int x) {
    return x + 1;
}
static int three(int x) {
    return x + 3;
}
static int (*pick(int w))(int) {
    return w ? one : three;
}
static int total(int n, ...) {
    va_list v;
    va_start(v, n);
    int s = 0;
    while (n--)
        s += va_arg(v, int);
    va_end(v);
    return s;
}
static int counter(void) {
    static int n;
    return ++n;
}
#define KIND(x) _Generic((x), int: "int", double: "double", default: "other")

int main(void) {
    union word w = {.i = 0x01020304};
    struct bits b = {.a = 5, .b = 17, .c = -3};
    struct bits *p = &(struct bits){.c = 8};
    int i = 0;
    printf("%d %d %d\n", red | blue, (int)sizeof(enum color), pick(1)(1));
    printf("%d %u %u %d\n", w.b[0], b.a, b.b, b.c);
    printf("%d %d %d\n", total(3, 1, 2, 3), counter(), counter());
    printf("%s %s %s\n", KIND(1), KIND(1.0), KIND("s"));
    printf("%d %zu\n", p->c, offsetof(struct bits, c));
loop:
    if (++i < 3) goto loop;
    printf("%d %s\n", i, __func__);
    return i - 3;
}
