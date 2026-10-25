#include <smith/contract.h>
int f(int x);
int h(int x);
int main(void) { return 0; }
static int g(int x) { return x; }
int f(int x) {
    smith_requires(x < 5);
    return x;
}
int h(int x) { return g(x); }
