#include <assert.h>
#include "fixtures/empty.c"

static_assert(0.5f + 0.5f == 1.0f, "exact");

int main(void) {
    return 0;
}
