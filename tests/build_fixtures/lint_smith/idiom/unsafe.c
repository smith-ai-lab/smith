#include <setjmp.h>
#include <stdlib.h>

static jmp_buf env;

void jump_out(void) {
    longjmp(env, 1);
}

int jump_in(void) {
    return setjmp(env);
}

unsigned first(unsigned xs[10]) {
    return xs[0];
}

unsigned sized(unsigned long n) {
    unsigned run_time[n];
    void *raw = alloca(n);
    return run_time[0] + (raw == nullptr ? 0 : 1);
}
