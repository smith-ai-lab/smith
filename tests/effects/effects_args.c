// [[smith::effects(...)]]: a ceiling with the names as written (LLVM r9, patches/clang-plugin-attr-args.patch).
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

[[smith::effects(alloc, net)]] void *promised(unsigned size);

[[smith::pure]] void *breaks(unsigned size) {
    return promised(size);
}

[[smith::effects(alloc, net)]] void *keeps(unsigned size) {
    return promised(size);
}
