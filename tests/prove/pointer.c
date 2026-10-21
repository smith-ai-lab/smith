// A call through a function pointer type with no contract: the target is not known.
#include <smith/contract.h>

#include <stdint.h>

typedef int32_t (*Operation)(int32_t);

int32_t apply(Operation operation, int32_t x) {
    smith_requires(0 <= x);
    return operation(x);
}
