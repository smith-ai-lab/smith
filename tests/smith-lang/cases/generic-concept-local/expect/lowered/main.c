#include <stdio.h>
typedef struct {
int a;
} Pair;
int scaled(int k) {
return k * 3;
}
int main(void) {
integer n = 3;
floating ratio = 0.5;
arithmetic total = n + scaled(2);
signed_integer delta = -4;
unsigned_integer bits = 12u;
pointer where = &n;
structure pair = (Pair){7};
printf("%d %g %d %d %u %d %d\n", n, ratio, total, delta, bits, *(int *)where, pair.a);
return 0;
}
