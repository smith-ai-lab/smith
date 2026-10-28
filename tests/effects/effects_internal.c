// The forms that are accepted and not taught: one attribute for each effect, and one GNU attribute with the names as
// strings. They mean what [[smith::effects(...)]] means.
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

[[smith::alloc, smith::files, smith::globals]] void *both(unsigned size) {
    FILE *file = fopen("data", "r");
    (void)file;
    return malloc(size);
}

__attribute__((smith_effects("alloc", "net"))) void *gnu(unsigned size) {
    return malloc(size);
}
