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
 [ [ clang :: unavailable ( "main::add_n is a generic function: call it from a .smith module" ) ] ] void main_add_n ( void ) ;
static __INT32_TYPE__ main_after(__INT32_TYPE__ x) {
return __extension__({extern __INT32_TYPE__ main_add_n__2(__INT32_TYPE__ x)__asm__("_main::add_n(2)");main_add_n__2(x);});
}
#endif

__INT32_TYPE__ main(void) {
printf("%d %d\n",__extension__({extern __INT32_TYPE__ main_add_n__3(__INT32_TYPE__ x)__asm__("_main::add_n(3)");main_add_n__3(10);}), main_after(1));
return 0;
}
extern __INT32_TYPE__ main_add_n__2(__INT32_TYPE__ x)__asm__("_main::add_n(2)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ main_add_n__2(__INT32_TYPE__ x){__INT32_TYPE__ sum=x;{sum+=1;}{sum+=1;}return sum;}
extern __INT32_TYPE__ main_add_n__3(__INT32_TYPE__ x)__asm__("_main::add_n(3)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ main_add_n__3(__INT32_TYPE__ x){__INT32_TYPE__ sum=x;{sum+=1;}{sum+=1;}{sum+=1;}return sum;}
