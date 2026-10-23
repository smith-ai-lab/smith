// A proof whose queries grow without end: 400 passes of a hash through a table the solver models as a memory array. It
// uses about 0.9 GB in the 10 s it is given, so a limit of 0.3 GB stops it first, and says so.
#include <smith/contract.h>

#include <stddef.h>
#include <stdint.h>

uint64_t blowup(uint8_t *table, uint64_t x) {
    smith_requires(table != nullptr);
    uint64_t total = x;
    for (size_t i = 0; i < 400; i++) {
        table[(total * 2654435761u) % 1000003] ^= (uint8_t)total;
        total = total * 6364136223846793005u + table[(total >> 7) % 1000003];
    }
    return total;
}
