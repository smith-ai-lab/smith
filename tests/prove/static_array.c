// `T p[static N]` is N objects, so a load of any of them is defined (#74: learner's world_kinds_of).
#include <smith/contract.h>

#include <stdint.h>

uint32_t third(const uint32_t values[static 4]) {
    smith_ensures(true);
    return values[3];
}

uint32_t past(const uint32_t values[static 4]) {
    smith_ensures(true);
    return values[4];
}
