// A callee whose smith_requires comes after a write: callers cannot use its contract, and the message names the write
// and the read.
#include <smith/contract.h>

#include <stdint.h>

typedef struct {
    int32_t a;
    int32_t b;
} Pair;

void swap_halves(Pair *pair) {
    int32_t saved = pair->a;
    pair->a = pair->b;
    smith_requires(pair->b >= 0);
    pair->b = saved;
    smith_ensures(pair->a >= 0);
}

void swaps_twice(Pair *pair) {
    smith_requires(pair->a >= 0 && pair->b >= 0);
    swap_halves(pair);
    swap_halves(pair);
}
