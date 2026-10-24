#include <stdio.h>

// `defer` is an ordinary name without a `module` line, as in C (issue #210): a variable, a function, a member.
struct hold {
    int defer;
};

static int defer_twice(int defer) {
    return defer * 2;
}

int main(void) {
    int defer = 1;
    struct hold h = {.defer = 4};
    printf("%d %d %d\n", defer, defer_twice(h.defer), defer + h.defer);
    return 0;
}
