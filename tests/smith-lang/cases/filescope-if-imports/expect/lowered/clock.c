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
#if SMITH_TARGET_OS == 1
#include <mach/mach_time.h>
#elif SMITH_TARGET_OS == 2
#include <time.h>
#else
#include <no_such_header_anywhere.h>
#endif
#include <stdint.h>

bool clock_ticks(void) __asm__("_clock::ticks");
bool clock_ticks(void) {
#if SMITH_TARGET_OS == 1
 {
return mach_absolute_time() > 0;
}
#else
 {
struct timespec now = {};
return clock_gettime(CLOCK_MONOTONIC, &now) == 0;
}
#endif

}
void clock_say(void) __asm__("_clock::say");
void clock_say(void) {
printf("%d %d\n", clock_ticks() ? 1 : 0, (__INT32_TYPE__)sizeof(uint32_t));
}
