#include "chain09.h"
#include "chain08.h"

__INT32_TYPE__ chain10_value(void) __asm__("_chain10::value");
__INT32_TYPE__ chain10_value(void) {
return 10;
}
__INT32_TYPE__ chain10_depth(void) __asm__("_chain10::depth");
__INT32_TYPE__ chain10_depth(void) {
return chain09_depth() + 1 + chain08_value() * 0;
}
