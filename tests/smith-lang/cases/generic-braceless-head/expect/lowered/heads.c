#include <math.h>
#include "same.h"
struct smith_enum_option_usize {enum smith_enum_option_usize_tag {smith_enum_option_usize_tag_some, smith_enum_option_usize_tag_none } tag; union { __SIZE_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
static struct smith_enum_option_usize heads_first_odd(__SIZE_TYPE__ *xs, __SIZE_TYPE__ n) {
for (__SIZE_TYPE__ i = 0; i < n; i++)
if (xs[i] % 2 == 1) return {.some = xs[i]};
return .none;
}
static Bool heads_fifth(struct smith_enum_option_usize at) {
if (at == .some) return at.some == 5;
return false;
}
__INT32_TYPE__ heads_run(void) __asm__("_heads::run");
__INT32_TYPE__ heads_run(void) {
__SIZE_TYPE__ xs[4] = {2, 4, 5, 6};
__SIZE_TYPE__ total = 0;
for (__SIZE_TYPE__ i = 0; i < 4; i++)
if (smith_call_failed) total += i;
for (__SIZE_TYPE__ i = 0; i < 4; i++)
total +=smith_call_failed ? 1u : 0u;
for (__SIZE_TYPE__ i = 0; i < 4; i++) total +=smith_call_failed ? 1u : 0u;
for (__SIZE_TYPE__ i = 0; i < 2; i++)
for (__SIZE_TYPE__ j = 0; j < 2; j++)
if (smith_call_failed) total += 10;
for (__INT32_TYPE__ log = 0; log < 3; log++) {
total += (__SIZE_TYPE__)log;
}
__SIZE_TYPE__ n = 3;
while (smith_call_failed)
n = 4;
return heads_fifth(heads_first_odd(xs, 4)) && total == 31 && n == 4 ? 0 : 1;
}
