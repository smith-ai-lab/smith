#ifndef SMITH_TARGET_ARCH
#if defined(__aarch64__)
#define SMITH_TARGET_ARCH 1
#elif defined(__x86_64__)
#define SMITH_TARGET_ARCH 2
#else
#define SMITH_TARGET_ARCH 0
#endif
#endif
#ifndef SMITH_TARGET_OS
#if defined(__APPLE__) && defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
#define SMITH_TARGET_OS 1
#elif defined(__linux__)
#define SMITH_TARGET_OS 2
#else
#define SMITH_TARGET_OS 0
#endif
#endif
#ifndef SMITH_TARGET_ASSERTIONS
#ifdef NDEBUG
#define SMITH_TARGET_ASSERTIONS 0
#else
#define SMITH_TARGET_ASSERTIONS 1
#endif
#endif
#include "kind.h"
#include <stdio.h>

constexpr bool fast = SMITH_TARGET_ASSERTIONS == false;
int main() {
__INT32_TYPE__ answer = 0;
#if SMITH_TARGET_OS == 1
 {
answer = 7;
}
#else
 {
undeclared_function();
}
#endif
#if (SMITH_TARGET_ASSERTIONS == 0)
 {
printf("assertions are off\n");
}
#elif SMITH_TARGET_ARCH == 1
 {
printf("answer is seven\n");
}
#else
 {
printf("neither\n");
}
#endif

printf("%d %d %d\n", kind_arch_name(), kind_opaque(5), kind_nested(20));
printf("%d %d %d\n",__extension__({extern __INT32_TYPE__ kind_describe__f32(float value)__asm__("_kind::describe(float)");kind_describe__f32(1.5f);}),__extension__({extern __INT32_TYPE__ kind_describe__u64(__UINT64_TYPE__ value)__asm__("_kind::describe(__UINT64_TYPE__)");kind_describe__u64(9);}),__extension__({extern __INT32_TYPE__ kind_describe__u8(__UINT8_TYPE__ value)__asm__("_kind::describe(__UINT8_TYPE__)");kind_describe__u8(1);}));
return answer == 7 ? 0 : 1;
}
extern __INT32_TYPE__ kind_describe__f32(float value)__asm__("_kind::describe(float)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ kind_describe__f32(float value){(void)value;}
extern __INT32_TYPE__ kind_describe__u64(__UINT64_TYPE__ value)__asm__("_kind::describe(__UINT64_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ kind_describe__u64(__UINT64_TYPE__ value){(void)value;}
extern __INT32_TYPE__ kind_describe__u8(__UINT8_TYPE__ value)__asm__("_kind::describe(__UINT8_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ kind_describe__u8(__UINT8_TYPE__ value){(void)value;}
