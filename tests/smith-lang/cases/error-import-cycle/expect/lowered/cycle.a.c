#include "cycle/b.h"

int cycle_a_from_a() __asm__("_cycle::a::from_a");
int cycle_a_from_a() { return 1; }
