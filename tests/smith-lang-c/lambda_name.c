#include <stdio.h>

// `lambda` is an ordinary name without a `module` line, as in C (issue #957): a function called with a function type's
// look-alike arguments, and a typedef used as a parameter's type.
typedef int lambda;

static int apply(lambda (*f)(int), int x) {
    return f(x);
}

static int twice(int x) {
    return x * 2;
}

static lambda lambda_of(lambda lambda_value) {
    return lambda_value + 1;
}

int main(void) {
    lambda value = lambda_of(1);
    printf("%d %d\n", apply(twice, 4), value);
    return 0;
}
