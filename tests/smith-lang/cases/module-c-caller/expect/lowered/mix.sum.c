__INT64_TYPE__ mix_sum_add(__INT64_TYPE__ a, __INT64_TYPE__ b) __asm__("_mix::sum::add");
__INT64_TYPE__ mix_sum_add(__INT64_TYPE__ a, __INT64_TYPE__ b) { return a + b; }
extern __INT64_TYPE__ mix_sum_total __asm__("_mix::sum::total");
__INT64_TYPE__ mix_sum_total __asm__("_mix::sum::total") = 100;
