#include "chain18.h"
#include "chain17.h"

__INT32_TYPE__ chain19_value(void) __asm__("_chain19::value");
__INT32_TYPE__ chain19_value(void) {
return 19;
}
__INT32_TYPE__ chain19_depth(void) __asm__("_chain19::depth");
__INT32_TYPE__ chain19_depth(void) {
return chain18_depth() + 1 + chain17_value() * 0;
}
