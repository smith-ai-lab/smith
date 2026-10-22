#include "chain38.h"
#include "chain37.h"

__INT32_TYPE__ chain39_value(void) __asm__("_chain39::value");
__INT32_TYPE__ chain39_value(void) {
return 39;
}
__INT32_TYPE__ chain39_depth(void) __asm__("_chain39::depth");
__INT32_TYPE__ chain39_depth(void) {
return chain38_depth() + 1 + chain37_value() * 0;
}
