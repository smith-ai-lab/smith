#include "chain08.h"
#include "chain07.h"

__INT32_TYPE__ chain09_value(void) __asm__("_chain09::value");
__INT32_TYPE__ chain09_value(void) {
return 9;
}
__INT32_TYPE__ chain09_depth(void) __asm__("_chain09::depth");
__INT32_TYPE__ chain09_depth(void) {
return chain08_depth() + 1 + chain07_value() * 0;
}
