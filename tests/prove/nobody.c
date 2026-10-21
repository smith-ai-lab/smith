// A function with neither a body nor a contract.
#include <smith/contract.h>

#include <stdint.h>

int32_t lookup(int32_t key);

int32_t uses(int32_t x) {
    smith_requires(0 < x);
    return lookup(x);
}
