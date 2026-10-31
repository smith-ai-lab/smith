// A `?:` runs one arm. Code generation asks the constant evaluator whether an arm is cheap enough to run
// unconditionally; a C function is not run for that question, so only the chosen arm's call happens (#1018).
#include <stdio.h>

static int calls;

static const char *first(void) {
    calls++;
    return "first";
}
static const char *second(void) {
    calls += 10;
    return "second";
}
static const char *pick(unsigned long h) {
    return h == 0 ? second() : first();
}

int main(void) {
    const char *a = pick(0);
    const char *b = pick(1);
    printf("%s %s %d\n", a, b, calls);
    return 0;
}
