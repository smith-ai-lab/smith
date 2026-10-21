#include <stdio.h>

#include "sys/world.h"

uint32_t foreign_counter = 5;
uint32_t sys_world_owned = 9;

uint32_t foreign_triple(uint32_t x) {
    return x * 3;
}

int main(void) {
    printf("%d %u %u\n", sys_world_env_count() > 0, sys_world_counter_times_three(), sys_world_owned);
    return 0;
}
