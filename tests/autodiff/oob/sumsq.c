#include "sumsq.h"

double sumsq(const double *p, size_t n, size_t extra) {
    double total = 0.0;
    for (size_t i = 0; i < n + extra; i++) {
        total += p[i] * p[i];
    }
    return total;
}
