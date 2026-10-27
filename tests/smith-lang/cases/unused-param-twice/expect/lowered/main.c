#include <stdio.h>

void main_first(__INT32_TYPE__ *_t __attribute__((unused))) __asm__("_main::first");
void main_first(__INT32_TYPE__ *_t __attribute__((unused))) {}
void main_second(__INT32_TYPE__ *_t __attribute__((unused)), __INT32_TYPE__ x) __asm__("_main::second");
void main_second(__INT32_TYPE__ *_t __attribute__((unused)), __INT32_TYPE__ x) {
printf("%d\n", x);
}
__INT32_TYPE__ main(void) {
main_second(nullptr, 3);
return 0;
}
