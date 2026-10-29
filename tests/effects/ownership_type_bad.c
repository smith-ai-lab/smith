#include <stdlib.h>

typedef int (*A)(void) [[clang::annotate_type("std::owns:internal")]];
typedef char *(*B)(const char *) [[clang::annotate_type("std::owns:owned:nonexistent")]];
typedef char *(*C)(const char *, int) [[clang::annotate_type("std::owns:borrowed:3")]];
typedef char *(*D)(const char *, int) [[clang::annotate_type("std::owns:borrowed:2")]];
typedef char *(*E)(const char *) [[clang::annotate_type("std::owns:owned:abs")]];
typedef char *(*F)(const char *) [[clang::annotate_type("std::owns:owned:free")]];
typedef char *(*G)(const char *) [[clang::annotate_type("std::owns:internal")]] [[clang::annotate_type("std::owns:borrowed:1")]];
