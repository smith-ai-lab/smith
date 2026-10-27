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
#include <sys/types.h>
#include <sys/stat.h>
#endif

FILE *report_output(void) __asm__("_report::output");
FILE *report_output(void) {
return nullptr;
}
void report_say(void) __asm__("_report::say");
void report_say(void) {
printf("report %d\n", report_output() == nullptr ? 1 : 0);
}
