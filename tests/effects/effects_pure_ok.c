// Pure: nothing it calls touches the world (issue #207). Crashing is `fails`, not pure (issue #325).
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int twice(int n) {
    return n * 2;
}

[[smith::pure]] int length(const char *text) {
    return (int)strlen(text) + twice(1);
}

[[smith::pure]] double root(double x) {
    return sqrt(x) + fabs(x);
}

[[smith::pure]] int fib(int n) { // recursion is no effect
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

[[smith::effects(fails)]] int crash(int n) { // abort and a trap stop the program: `fails`
    if (n < 0) abort();
    if (n > 100) __builtin_trap();
    return n;
}
