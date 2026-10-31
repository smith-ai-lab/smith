static inline __INT32_TYPE__ pair_twice(__INT32_TYPE__ value) {
return value * 2;
}
static __INT32_TYPE__ pair_hidden(__INT32_TYPE__ value) {
return value + 1;
}
__INT32_TYPE__ pair_first() __asm__("_pair::first");
__INT32_TYPE__ pair_first() {
return pair_hidden(0);
}
