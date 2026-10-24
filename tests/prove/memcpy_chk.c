// __memcpy_chk (what Apple's headers make of memcpy with a destination of known size) has no body: it is memcpy.
// The proof sees the copy: a value copied is the value read back, and a length past the destination is refuted.
#include <smith/contract.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

uint32_t copied(uint32_t value) {
    uint32_t from[4] = {value, 2, 3, 4};
    uint32_t to[4] = {0, 0, 0, 0};
    __builtin___memcpy_chk(to, from, sizeof to, __builtin_object_size(to, 0));
    smith_ensures(to[0] == value);
    return to[0];
}

uint32_t too_long(size_t length) {
    smith_requires(length <= 32);
    uint32_t from[4] = {1, 2, 3, 4};
    uint32_t to[4] = {0, 0, 0, 0};
    __builtin___memcpy_chk(to, from, length, __builtin_object_size(to, 0));
    return to[0];
}
