// `undefined` is a module's reserved name (issue #743); with no `module` line it is an ordinary name, global, local,
// field and assignment included.
#include <stdio.h>

static int undefined = 7;
struct holder {
    int undefined;
};

int main(void) {
    int copy = undefined;
    undefined = copy + 1;
    struct holder h = {.undefined = undefined + 1};
    int local_undefined = 3;
    {
        int undefined = local_undefined;
        printf("%d %d %d\n", undefined, h.undefined, copy);
    }
    printf("%d\n", undefined);
    return 0;
}
