#include "chain04.h"
#include "chain03.h"

__INT32_TYPE__ chain05_value(void) __asm__("_chain05::value");
__INT32_TYPE__ chain05_value(void) {
return 5;
}
__INT32_TYPE__ chain05_depth(void) __asm__("_chain05::depth");
__INT32_TYPE__ chain05_depth(void) {
return chain04_depth() + 1 + chain03_value() * 0;
}
