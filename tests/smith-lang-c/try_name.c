#include <stdio.h>

// `try` and `errdefer` are ordinary names without a `module` line, as in C (issue #211): a variable, a function, a
// member.
struct hold {
    int try;
    int errdefer;
};

static int errdefer_twice(int errdefer) {
    return errdefer * 2;
}

int main(void) {
    int try = 1;
    int errdefer = 2;
    struct hold h = {.try = 3, .errdefer = 4};
    printf("%d %d %d\n", try + errdefer, errdefer_twice(h.errdefer), h.try + h.errdefer);
    return 0;
}
