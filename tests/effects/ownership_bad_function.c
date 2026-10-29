#include <stdlib.h>

[[std::returns_owned(nonexistent)]] char *a(const char *s);
[[std::returns_owned(free)]] int b(void);
[[std::returns_borrowed(3)]] char *c(const char *s, int n);
[[std::returns_borrowed(2)]] char *d(const char *s, int n);
[[std::returns_internal]] [[std::returns_borrowed(1)]] char *e(const char *s);
[[std::returns_owned(free)]] [[std::returns_owned(free)]] char *f(const char *s);
[[std::returns_owned(abs)]] char *g(const char *s);
[[std::returns_owned(free)]] char *h(const char *s);
[[std::returns_internal]] char *h(const char *s);
