// A call that stops the program is a failure point: `fails`, the effect of every checked index, assert and trap
// (issue #325).
#include <smith/trace.h>
#include <stdlib.h>

[[std::pure]] int quiet(int n) {
    return n + 1;
}

[[std::effects(fails)]] int may_fail(int n) {
    if (n < 0) abort();
    if (n > 100) __builtin_trap();
    if (n == 50) smith_failure(SmithFailure_Check);
    return n;
}

[[std::pure]] int aborts(int n) {
    if (n < 0) abort();
    return n;
}

[[std::pure]] int via(int n) {
    return may_fail(n);
}
