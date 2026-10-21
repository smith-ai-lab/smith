#pragma STDC FP_CONTRACT OFF
#include <string.h>
#ifdef __FAST_MATH__
#error "no -ffast-math"
#endif
#define PAIR(a, b) (((a) << 8) | (b))
#undef UNUSED_NAME
#if defined(__aarch64__)
static const char *demo_dir_machine = "arm64";
#else
static const char *demo_dir_machine = "other";
#endif
#if defined(__x86_64__)
__UINT32_TYPE__ demo_dir_width(__UINT32_TYPE__ x) __asm__("_demo::dir::width");
__UINT32_TYPE__ demo_dir_width(__UINT32_TYPE__ x) {
__UINT64_TYPE__ wide = x;
return (__UINT32_TYPE__)(wide + 1);
}
#elif defined(__aarch64__)
__UINT32_TYPE__ demo_dir_width(__UINT32_TYPE__ x) __asm__("_demo::dir::width");
__UINT32_TYPE__ demo_dir_width(__UINT32_TYPE__ x) {
__UINT64_TYPE__ wide = x;
return (__UINT32_TYPE__)(wide + 2);
}
#else
#error "unsupported machine"
#endif
__UINT32_TYPE__ demo_dir_pair(__UINT32_TYPE__ a, __UINT32_TYPE__ b) __asm__("_demo::dir::pair");
__UINT32_TYPE__ demo_dir_pair(__UINT32_TYPE__ a, __UINT32_TYPE__ b) {
return PAIR(a, b);
}
__SIZE_TYPE__ demo_dir_name_length(void) __asm__("_demo::dir::name_length");
__SIZE_TYPE__ demo_dir_name_length(void) {
return strlen(demo_dir_machine);
}
