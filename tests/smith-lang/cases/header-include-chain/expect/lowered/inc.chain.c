#include <stdint.h>
#if defined(INC_FIRST)
#include <stdio.h>
#elif defined(INC_SECOND)
#include <stdlib.h>
#if defined(INC_DEEP)
#include <ctype.h>
#endif
#else
#include <string.h>
#endif
#ifdef UNRELATED
#define NOTHING 1
#endif
#include <math.h>
#if __has_include("chain_extra.h")
#include "chain_extra.h"
#endif
__UINT32_TYPE__ inc_chain_length(const char *text) __asm__("_inc::chain::length");
__UINT32_TYPE__ inc_chain_length(const char *text) {
return (__UINT32_TYPE__)strlen(text);
}
double inc_chain_root(double value) __asm__("_inc::chain::root");
double inc_chain_root(double value) {
return sqrt(value);
}
