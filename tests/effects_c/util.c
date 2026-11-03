// C that a smith function calls: quiet is pure, helper prints.
#include <stdio.h>

int quiet(int x) {
    return x + 1;
}

int helper(int x) {
    printf("%d\n", x);
    return x;
}
