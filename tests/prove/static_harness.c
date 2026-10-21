// Two specification harnesses written as static functions that nothing calls: clang emits neither, except the one marked
// [[gnu::used]].
#include <smith/contract.h>

#include <stdint.h>

static int32_t harness_dropped(int32_t x) {
    smith_requires(0 < x);
    smith_ensures(0 < x);
    return x;
}

[[gnu::used]] static int32_t harness_kept(int32_t x) {
    smith_requires(0 < x);
    smith_ensures(0 < x);
    return x;
}

int32_t no_contract(int32_t x) { return x + 1; }
