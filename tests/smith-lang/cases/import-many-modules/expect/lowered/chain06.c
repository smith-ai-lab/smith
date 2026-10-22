#include "chain05.h"
#include "chain04.h"

__INT32_TYPE__ chain06_value(void) __asm__("_chain06::value");
__INT32_TYPE__ chain06_value(void) {
return 6;
}
__INT32_TYPE__ chain06_depth(void) __asm__("_chain06::depth");
__INT32_TYPE__ chain06_depth(void) {
return chain05_depth() + 1 + chain04_value() * 0;
}
