#include <stdlib.h>
#include <string.h>

[[std::returns_owned(free)]] char *own_dup(const char *s);
[[std::returns_borrowed(1)]] char *own_find(const char *s, int c);
[[std::returns_internal]] char *own_message(int errnum);

[[std::returns_internal]] typedef char *(*Describe)(int);
typedef char *(*Name)(int) [[clang::annotate_type("std::owns:internal")]];

[[std::returns_owned(free)]] char *own_dup(const char *s) {
    return strdup(s);
}
typedef char *(*Duplicate)(const char *) [[clang::annotate_type("std::owns:owned:free")]];
typedef char *(*Find)(const char *, int) [[clang::annotate_type("std::owns:borrowed:1")]];
[[std::returns_borrowed(1)]] char *again(const char *s);
[[std::returns_borrowed(1)]] char *again(const char *s) {
    return (char *)s;
}
