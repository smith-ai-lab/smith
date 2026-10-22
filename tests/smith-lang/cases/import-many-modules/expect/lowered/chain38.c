#include "chain37.h"
#include "chain36.h"

__INT32_TYPE__ chain38_value(void) __asm__("_chain38::value");
__INT32_TYPE__ chain38_value(void) {
return 38;
}
__INT32_TYPE__ chain38_depth(void) __asm__("_chain38::depth");
__INT32_TYPE__ chain38_depth(void) {
return chain37_depth() + 1 + chain36_value() * 0;
}
