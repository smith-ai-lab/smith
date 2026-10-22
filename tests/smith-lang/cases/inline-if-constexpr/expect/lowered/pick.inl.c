#ifndef SMITH_TARGET_ADDRESS_SANITIZER
#if __has_feature(address_sanitizer)
#define SMITH_TARGET_ADDRESS_SANITIZER 1
#else
#define SMITH_TARGET_ADDRESS_SANITIZER 0
#endif
#endif
#ifndef SMITH_TARGET_ASSERTIONS
#ifdef NDEBUG
#define SMITH_TARGET_ASSERTIONS 0
#else
#define SMITH_TARGET_ASSERTIONS 1
#endif
#endif
#include <stdio.h>

static inline __INT32_TYPE__ pick_inl_flavor(void) {
#if SMITH_TARGET_ADDRESS_SANITIZER
 {
return 1;
}
#elif SMITH_TARGET_ASSERTIONS
 {
return 2;
}
#else
 {
return 3;
}
#endif

}
void pick_inl_show(void) __asm__("_pick::inl::show");
void pick_inl_show(void) {
printf("flavor %d\n", pick_inl_flavor());
}
