#include <stdio.h>

extern const __INT32_TYPE__ main_primes[] __asm__("_main::primes");
const __INT32_TYPE__ main_primes[] __asm__("_main::primes") = {2, 3, 5, 7};
static __INT32_TYPE__ main_sum(const __INT32_TYPE__ *xs, __SIZE_TYPE__ n) {
__INT32_TYPE__ total = 0;
for (__SIZE_TYPE__ i = 0; i < n; i++)
total += xs[i];
return total;
}
__INT32_TYPE__ main(void) {
__INT32_TYPE__ xs[] = {1, 2, 3};
__INT32_TYPE__ m[][3] = {{1, 2, 3}, {4, 5, 6}};
char s[] = "abc";
printf("%zu %zu %zu %zu\n", sizeof xs / sizeof xs[0], sizeof m / sizeof m[0], sizeof s, sizeof main_primes / sizeof main_primes[0]);
printf("%d %d\n", main_sum(xs, 3), main_sum((const __INT32_TYPE__[]){10, 20}, 2));
return 0;
}
