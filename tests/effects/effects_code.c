// Making memory executable is the effect `code` (issue #396): mmap and mprotect with a constant protection that has
// PROT_EXEC. A protection that is not read-execute, or not a constant, stays what the table says (`alloc`).
#include <sys/mman.h>

[[std::effects(alloc)]] void *jit_page(unsigned long size) {
    return mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
}

[[std::effects(alloc)]] int seal(void *page, unsigned long size) {
    return mprotect(page, size, PROT_READ | PROT_EXEC);
}

[[std::effects(alloc)]] void *data_page(unsigned long size) {
    return mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
}

[[std::effects(alloc)]] int unknown_protection(void *page, unsigned long size, int protection) {
    return mprotect(page, size, protection);
}

[[std::effects(alloc, code)]] void *jit_page_declared(unsigned long size) {
    return mmap(nullptr, size, PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
}
