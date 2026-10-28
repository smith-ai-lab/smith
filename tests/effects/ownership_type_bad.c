#include <stdlib.h>

typedef int (*A)(void) [[clang::annotate_type("smith::owns:internal")]];
typedef char *(*B)(const char *) [[clang::annotate_type("smith::owns:owned:nonexistent")]];
typedef char *(*C)(const char *, int) [[clang::annotate_type("smith::owns:borrowed:3")]];
typedef char *(*D)(const char *, int) [[clang::annotate_type("smith::owns:borrowed:2")]];
typedef char *(*E)(const char *) [[clang::annotate_type("smith::owns:owned:abs")]];
typedef char *(*F)(const char *) [[clang::annotate_type("smith::owns:owned:free")]];
