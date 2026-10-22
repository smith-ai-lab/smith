#include "chain00.h"

__INT32_TYPE__ chain01_value(void) __asm__("_chain01::value");
__INT32_TYPE__ chain01_value(void) {
return 1;
}
__INT32_TYPE__ chain01_depth(void) __asm__("_chain01::depth");
__INT32_TYPE__ chain01_depth(void) {
return chain00_depth() + 1;
}
