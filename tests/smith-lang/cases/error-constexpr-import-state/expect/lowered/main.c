#include "counter.h"

static_assert(counter_plain(1) == 2);
static_assert(counter_next() == 1);
int main(void) {
return 0;
}
