// A struct zero-initialized before a smith_requires reads memory is a write of a local, not of the caller's memory,
// so it does not stop callers from using the contract.
#include <smith/contract.h>

#include <stdint.h>

typedef struct {
    int32_t cells[8];
    int32_t count;
} Bag;

void fill(Bag *bag, int32_t value) {
    Bag fresh = {};
    smith_requires(bag->count == 0);
    fresh.cells[0] = value;
    *bag = fresh;
    smith_ensures(bag->cells[0] == value);
}

void fill_once(Bag *bag) {
    smith_requires(bag->count == 0);
    fill(bag, 3);
    smith_ensures(bag->cells[0] == 3);
}
