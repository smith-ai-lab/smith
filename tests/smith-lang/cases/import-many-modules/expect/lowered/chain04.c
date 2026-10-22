#include "chain03.h"
#include "chain02.h"

__INT32_TYPE__ chain04_value(void) __asm__("_chain04::value");
__INT32_TYPE__ chain04_value(void) {
return 4;
}
__INT32_TYPE__ chain04_depth(void) __asm__("_chain04::depth");
__INT32_TYPE__ chain04_depth(void) {
return chain03_depth() + 1 + chain02_value() * 0;
}
