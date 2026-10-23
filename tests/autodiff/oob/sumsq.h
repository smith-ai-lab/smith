#pragma once
#include <stddef.h>

// The sum of squares of p[0 .. n + extra): with extra nonzero it reads past the end.
double sumsq(const double *p, size_t n, size_t extra);
