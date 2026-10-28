#include "cycle/a.h"

int cycle_b_from_b() __asm__("_cycle::b::from_b");
int cycle_b_from_b() { return 2; }
