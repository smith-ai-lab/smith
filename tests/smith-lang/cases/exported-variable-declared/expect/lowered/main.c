#include "counter/state.h"
#include <stdio.h>

__INT32_TYPE__ main() {
counter_state_total = 5;
printf("%u %s %llu\n", counter_state_sum(), counter_state_names[1], (unsigned long long)counter_state_chosen);
return 0;
}
