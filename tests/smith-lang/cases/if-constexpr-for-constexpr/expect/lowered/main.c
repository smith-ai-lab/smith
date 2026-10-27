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
return smith_call_failed;
}
#endif

__INT32_TYPE__ main(void) {
printf("%d %d\n",smith_call_failed, main_after(1));
return 0;
}
