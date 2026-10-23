// An out-of-bounds read in a differentiated function: `main oob primal` calls it, `main oob gradient` takes its
// gradient, and `main ok` stays inside the array. AddressSanitizer (debug profile) must stop the first two at the line
// of the read.
#include <smith/autodiff.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sumsq.h"

int main(int argc, char *argv[]) {
    if (argc != 2) return 2;
    size_t n = 8;
    size_t extra = strcmp(argv[1], "ok") == 0 ? 0 : 1;
    double *p = malloc(n * sizeof(double));
    double *dp = calloc(n + 1, sizeof(double));
    for (size_t i = 0; i < n; i++)
        p[i] = (double)i + 0.5;
    if (strcmp(argv[1], "gradient") == 0 || strcmp(argv[1], "ok") == 0) {
        __enzyme_autodiff((void *)sumsq, enzyme_dup, p, dp, enzyme_const, n, enzyme_const, extra);
        printf("gradient %.3f %.3f\n", dp[0], dp[n - 1]);
    } else {
        printf("primal %.3f\n", sumsq(p, n, extra));
    }
    free(p);
    free(dp);
    return 0;
}
