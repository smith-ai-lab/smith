#ifndef SMITH_TARGET_OS
#if defined(__APPLE__) && defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
#define SMITH_TARGET_OS 1
#elif defined(__linux__)
#define SMITH_TARGET_OS 2
#else
#define SMITH_TARGET_OS 0
#endif
#endif
#if SMITH_TARGET_OS == 1 || SMITH_TARGET_OS == 2
#include <stdint.h>
#endif
#include <stddef.h>
#include <stdio.h>
 [ [ clang :: unavailable ( "count::count_of is a generic function: call it from a .smith module" ) ] ] void count_count_of ( void ) ; [ [ clang :: unavailable ( "count::show is a generic function: call it from a .smith module" ) ] ] void count_show ( void ) ;
