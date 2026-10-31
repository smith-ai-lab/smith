#ifndef SMITH_TARGET_OS
#if defined(__APPLE__) && defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
#define SMITH_TARGET_OS 1
#elif defined(__linux__)
#define SMITH_TARGET_OS 2
#else
#define SMITH_TARGET_OS 0
#endif
#endif
#include <stdio.h>
#if SMITH_TARGET_OS == 1 || SMITH_TARGET_OS != 1
 [ [ clang :: unavailable ( "main::sum_fields is a generic function: call it from a .smith module" ) ] ] void main_sum_fields ( void ) ;
struct main_Pair {
__INT32_TYPE__ a;
__INT32_TYPE__ b;
};
typedef struct main_Pair main_Pair;
static __INT32_TYPE__ main_after(main_Pair *p) {
return __extension__({extern __INT32_TYPE__ main_sum_fields__Pair(main_Pair*x)__asm__("_main::sum_fields(main_Pair)");main_sum_fields__Pair(p);});
}
#endif

__INT32_TYPE__ main(void) {
main_Pair p = {1, 2};
printf("%d\n", main_after(&p));
return 0;
}
extern __INT32_TYPE__ main_sum_fields__Pair(main_Pair*x)__asm__("_main::sum_fields(main_Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ main_sum_fields__Pair(main_Pair*x){__INT32_TYPE__ sum=0;return sum;}
