#include "chain07.h"
#include "chain06.h"

__INT32_TYPE__ chain08_value(void) __asm__("_chain08::value");
__INT32_TYPE__ chain08_value(void) {
return 8;
}
__INT32_TYPE__ chain08_depth(void) __asm__("_chain08::depth");
__INT32_TYPE__ chain08_depth(void) {
return chain07_depth() + 1 + chain06_value() * 0;
}
