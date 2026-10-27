#include <stdio.h>

typedef struct {
__INT32_TYPE__ _reserved __attribute__((unused));
__INT32_TYPE__ value;
} main_Item;
[[nodiscard]] static __INT32_TYPE__ main_compute(__INT32_TYPE__ x) {
return x * 2;
}
__INT32_TYPE__ main_pick(__INT32_TYPE__ smith_unused_1 __attribute__((unused)), __INT32_TYPE__ smith_unused_2 __attribute__((unused)), __INT32_TYPE__ third) __asm__("_main::pick");
__INT32_TYPE__ main_pick(__INT32_TYPE__ smith_unused_1 __attribute__((unused)), __INT32_TYPE__ smith_unused_2 __attribute__((unused)), __INT32_TYPE__ third) {
return third;
}
static void main_on_key(__INT32_TYPE__ *_window __attribute__((unused)), __INT32_TYPE__ key) {
printf("key %d\n", key);
}
__INT32_TYPE__ main(void) {
__INT32_TYPE__ window = 0;
main_Item item = {.value = 4}; [[maybe_unused]] auto smith_discard_3 = main_compute(3); [[maybe_unused]] auto smith_discard_4 = main_compute(4);
main_on_key(&window, item.value + item._reserved);
__INT32_TYPE__ _spare __attribute__((unused)) = 1;
__INT32_TYPE__ smith_unused_5 __attribute__((unused)) = 5;
__INT32_TYPE__ smith_unused_6 __attribute__((unused)) = 6;
printf("%d\n", main_pick(1, 2, 3));
return 0;
}
