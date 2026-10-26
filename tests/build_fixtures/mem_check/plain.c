#include <string.h>

void copy_plain(char *dst, const char *src, unsigned long n) {
    memcpy(dst, src, n);
    memset(dst, 0, n);
}
