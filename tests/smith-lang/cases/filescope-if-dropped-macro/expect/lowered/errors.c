#if 1
#include <errno.h>
#else
#include <errno.h>
#endif

__INT32_TYPE__ errors_current() __asm__("_errors::current");
__INT32_TYPE__ errors_current() {
return errno;
}
