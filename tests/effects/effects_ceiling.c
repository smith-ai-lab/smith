// A ceiling: [[std::effects(alloc)]] may allocate and nothing else; several effects are listed in one attribute.
#include <stdio.h>
#include <stdlib.h>

[[std::effects(alloc)]] void *grab(unsigned size) {
    return malloc(size);
}

[[std::effects(alloc)]] void *too_much(unsigned size) {
    FILE *file = fopen("data", "r");
    (void)file;
    return malloc(size);
}

[[std::effects(alloc, files)]] void *both(unsigned size) {
    FILE *file = fopen("data", "r");
    (void)file;
    return malloc(size);
}

[[std::effects(alloc, net)]] void *with_net(unsigned size) {
    return malloc(size);
}
