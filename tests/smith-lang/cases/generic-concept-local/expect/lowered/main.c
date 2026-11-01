#include <stdio.h>
typedef struct {
int a;
} Pair;
int scaled(int k) {
return k * 3;
}
int main(void) {
Int n = 3;
Float ratio = 0.5;
Num total = n + scaled(2);
Signed delta = -4;
Unsigned bits = 12u;
Pointer where = &n;
Struct pair = (Pair){7};
printf("%d %g %d %d %u %d %d\n", n, ratio, total, delta, bits, *(int *)where, pair.a);
return 0;
}
