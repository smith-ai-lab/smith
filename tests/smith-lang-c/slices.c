// C that a run-time array could be mistaken for keeps its meaning: a text array keeps its zero, a `?:` inside brackets
// is the conditional, and a subscript is a subscript.
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int pick(int a[4], int x) {
    return a[x ? 1 : 2];
}

int main(void) {
    char word[] = "abc";
    int xs[3] = {1, 2, 3};
    int *p = xs;
    int bytes = (int)sizeof word;
    int chosen = xs[p[0] ? 2 : 0];
    size_t length = strlen(word);
    printf("%d %d %zu %d\n", bytes, pick(xs, 1), length, chosen);
    return 0;
}
