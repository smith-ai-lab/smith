static __INT32_TYPE__ counter_count = 0;
__INT32_TYPE__ counter_next() __asm__("_counter::next");
__INT32_TYPE__ counter_next() {
counter_count += 1;
return counter_count;
}
__INT32_TYPE__ counter_plain(__INT32_TYPE__ n) __asm__("_counter::plain");
__INT32_TYPE__ counter_plain(__INT32_TYPE__ n) {
return n + 1;
}
