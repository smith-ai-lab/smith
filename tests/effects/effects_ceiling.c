// A ceiling: [[smith::alloc]] may allocate and nothing else (one attribute for each effect).
#include <stdio.h>
#include <stdlib.h>

[[smith::alloc]] void *grab(unsigned size) {
    return malloc(size);
}

[[smith::alloc]] void *too_much(unsigned size) {
    FILE *file = fopen("data", "r");
    (void)file;
    return malloc(size);
}

[[smith::alloc, smith::files]] void *both(unsigned size) {
    FILE *file = fopen("data", "r");
    (void)file;
    return malloc(size);
}

__attribute__((smith_effects("alloc", "net"))) void *gnu(unsigned size) {
    return malloc(size);
}
