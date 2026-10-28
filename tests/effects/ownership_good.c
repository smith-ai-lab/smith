#include <stdlib.h>
#include <string.h>

[[smith::returns_owned(free)]] char *own_dup(const char *s);
[[smith::returns_borrowed(1)]] char *own_find(const char *s, int c);
[[smith::returns_internal]] char *own_message(int errnum);

[[smith::returns_internal]] typedef char *(*Describe)(int);
typedef char *(*Name)(int) [[clang::annotate_type("smith::owns:internal")]];

[[smith::returns_owned(free)]] char *own_dup(const char *s) {
    return strdup(s);
}
