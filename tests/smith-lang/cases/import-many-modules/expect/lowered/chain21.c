#include "chain20.h"
#include "chain19.h"

__INT32_TYPE__ chain21_value(void) __asm__("_chain21::value");
__INT32_TYPE__ chain21_value(void) {
return 21;
}
__INT32_TYPE__ chain21_depth(void) __asm__("_chain21::depth");
__INT32_TYPE__ chain21_depth(void) {
return chain20_depth() + 1 + chain19_value() * 0;
}
