// tests/prove/edge.c with the edge marked trusted, as the message says: printf is outside what the prover reads, show
// is assumed, and shown_twice is proved from show's contract.
#include <smith/contract.h>

#include <stdint.h>
#include <stdio.h>

static void show(int32_t value) {
    smith_trusted();
    smith_requires(value >= 0);
    printf("%d\n", value);
}

int32_t shown_twice(int32_t x) {
    smith_requires(x >= 0 && x < 1000);
    show(x);
    int32_t y = x * 2;
    smith_ensures(y == x * 2);
    return y;
}

// The same call with an argument show does not accept is refused at the call.
int32_t shown_negative(int32_t x) {
    smith_requires(x < 0);
    show(x);
    return x;
}
