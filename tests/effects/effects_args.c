// [[std::effects(...)]]: a ceiling with the names as written (LLVM r9, patches/clang-plugin-attr-args.patch).
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

[[std::effects(alloc, net)]] void *promised(unsigned size);

[[std::pure]] void *breaks(unsigned size) {
    return promised(size);
}

[[std::effects(alloc, net)]] void *keeps(unsigned size) {
    return promised(size);
}
