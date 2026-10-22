#include <stdio.h>

#include "hint.h"

int main(void) {
    hint_first(true);
    printf("%d %d\n", hint_second(1), hint_used(5));
    return 0;
}
