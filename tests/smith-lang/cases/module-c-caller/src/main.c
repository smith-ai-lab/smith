#include <stdio.h>

#include "mix/sum.h"

int main(void) {
    printf("%lld %lld\n", (long long)mix_sum_add(40, 2), (long long)mix_sum_total);
    return 0;
}
