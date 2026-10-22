#include "chain06.h"
#include "chain05.h"

__INT32_TYPE__ chain07_value(void) __asm__("_chain07::value");
__INT32_TYPE__ chain07_value(void) {
return 7;
}
__INT32_TYPE__ chain07_depth(void) __asm__("_chain07::depth");
__INT32_TYPE__ chain07_depth(void) {
return chain06_depth() + 1 + chain05_value() * 0;
}
