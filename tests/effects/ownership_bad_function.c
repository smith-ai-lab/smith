#include <stdlib.h>

[[smith::returns_owned(nonexistent)]] char *a(const char *s);
[[smith::returns_owned(free)]] int b(void);
[[smith::returns_borrowed(3)]] char *c(const char *s, int n);
[[smith::returns_borrowed(2)]] char *d(const char *s, int n);
[[smith::returns_internal]] [[smith::returns_borrowed(1)]] char *e(const char *s);
[[smith::returns_owned(free)]] [[smith::returns_owned(free)]] char *f(const char *s);
[[smith::returns_owned(abs)]] char *g(const char *s);
