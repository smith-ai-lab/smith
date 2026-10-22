#include "chain02.h"
#include "chain01.h"

__INT32_TYPE__ chain03_value(void) __asm__("_chain03::value");
__INT32_TYPE__ chain03_value(void) {
return 3;
}
__INT32_TYPE__ chain03_depth(void) __asm__("_chain03::depth");
__INT32_TYPE__ chain03_depth(void) {
return chain02_depth() + 1 + chain01_value() * 0;
}
