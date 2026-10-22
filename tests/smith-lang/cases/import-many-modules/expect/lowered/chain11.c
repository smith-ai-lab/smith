#include "chain10.h"
#include "chain09.h"

__INT32_TYPE__ chain11_value(void) __asm__("_chain11::value");
__INT32_TYPE__ chain11_value(void) {
return 11;
}
__INT32_TYPE__ chain11_depth(void) __asm__("_chain11::depth");
__INT32_TYPE__ chain11_depth(void) {
return chain10_depth() + 1 + chain09_value() * 0;
}
