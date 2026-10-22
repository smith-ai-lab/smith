#include "chain11.h"
#include "chain10.h"

__INT32_TYPE__ chain12_value(void) __asm__("_chain12::value");
__INT32_TYPE__ chain12_value(void) {
return 12;
}
__INT32_TYPE__ chain12_depth(void) __asm__("_chain12::depth");
__INT32_TYPE__ chain12_depth(void) {
return chain11_depth() + 1 + chain10_value() * 0;
}
