#include "chain01.h"
#include "chain00.h"

__INT32_TYPE__ chain02_value(void) __asm__("_chain02::value");
__INT32_TYPE__ chain02_value(void) {
return 2;
}
__INT32_TYPE__ chain02_depth(void) __asm__("_chain02::depth");
__INT32_TYPE__ chain02_depth(void) {
return chain01_depth() + 1 + chain00_value() * 0;
}
