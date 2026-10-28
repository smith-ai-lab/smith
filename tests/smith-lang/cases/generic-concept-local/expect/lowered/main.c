#include <stdio.h>

struct main_Pair {
__INT32_TYPE__ a;
};
typedef struct main_Pair main_Pair;
static __INT32_TYPE__ main_scaled(__INT32_TYPE__ k) {
return k * 3;
}
__INT32_TYPE__ main(void) {
smith_concepts_Int n = 3;
smith_concepts_Float ratio = 0.5;
smith_concepts_Num total = n + main_scaled(2);
smith_concepts_Signed delta = -4;
smith_concepts_Unsigned bits = 12u;
smith_concepts_Pointer where = &n;
smith_concepts_Struct pair = (main_Pair){7};
printf("%d %g %d %d %u %d %d\n", n, ratio, total, delta, bits, *(__INT32_TYPE__ *)where, pair.a);
return 0;
}
