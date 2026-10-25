#include <smith/contract.h>
#include <stdint.h>

void hot_begin(uint32_t *counter) {
    smith_requires(counter != nullptr && *counter < 100);
    *counter += 1;
}
