// C's for loops, one header after another: a `:` in a header is never a range, because C's `:` follows a `?`, and every
// header with two `;` is C's own. Each prints what it prints in a .c file.
#include <stdio.h>

static int pick(int a, int b, int c) {
    return a ? b : c;
}

int main(void) {
    int total = 0;
    int xs[4] = {1, 2, 3, 4};
    for (int i = pick(1, 2, 3); i < (total ? 9 : 5); i += (i > 2 ? 2 : 1))
        total += i;
    for (;;) {
        total += 100;
        break;
    }
    for (int i = 0, j = 3; i < 4; i++, j--)
        total += xs[i] * j;
    for (int i = 0; i < 4; ++i) {
        switch (xs[i]) {
        case 1:
            total += 1;
            break;
        default:
            total += xs[i] ? 10 : 20;
        }
    }
    int *p = xs;
    for (; p != xs + 4; p++)
        total += p[0] > 2 ? p[0] : -p[0];
    printf("%d\n", total);
    return 0;
}
