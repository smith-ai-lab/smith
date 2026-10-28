__INT32_TYPE__ num_twice(__INT32_TYPE__ x) __asm__("_num::twice");
__INT32_TYPE__ num_twice(__INT32_TYPE__ x) {
return x * 2;
}
__INT32_TYPE__ num_broken(__INT32_TYPE__ x) __asm__("_num::broken");
__INT32_TYPE__ num_broken(__INT32_TYPE__ x) {
return x + missing;
}
