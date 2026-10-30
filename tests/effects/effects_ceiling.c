// A ceiling: [[smith::effects(alloc)]] may allocate and nothing else; several effects are listed in one attribute.
#include <stdio.h>
#include <stdlib.h>

[[smith::effects(alloc)]] void *grab(unsigned size) {
    return malloc(size);
}

[[smith::effects(alloc)]] void *too_much(unsigned size) {
    FILE *file = fopen("data", "r");
    (void)file;
    return malloc(size);
}

[[smith::effects(alloc, files, globals)]] void *both(unsigned size) {
    FILE *file = fopen("data", "r");
    (void)file;
    return malloc(size);
}

[[smith::effects(alloc, net)]] void *with_net(unsigned size) {
    return malloc(size);
}
