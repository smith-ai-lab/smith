#include <stdio.h>
#include <string.h>

static constexpr __SIZE_TYPE__ main_width = 5;
static __SIZE_TYPE__ main_sizes(__INT32_TYPE__ xs[static main_width], float m[static 3][2]) {
return countof(xs) + countof(m) * 10 + countof(m[0]) * 100;
}
static __INT32_TYPE__ main_last(__INT32_TYPE__ xs[static main_width]) {
return xs[main_width - 1] + xs[0];
}
static __INT32_TYPE__ main_sum(const __INT32_TYPE__ xs[static main_width]) {
__INT32_TYPE__ total = 0;
for (__SIZE_TYPE__ i = 0; i < countof(xs); i++) {
total += xs[i];
}
return total;
}
static __INT32_TYPE__ main_pass_on(__INT32_TYPE__ xs[static main_width]) {
return main_last(xs) + main_sum(xs);
}
static __INT32_TYPE__ main_walk(__INT32_TYPE__ *xs, __SIZE_TYPE__ count) {
__INT32_TYPE__ total = 0;
for (__SIZE_TYPE__ i = 0; i < count; i++) {
total += *xs++;
}
return total;
}
__INT32_TYPE__ main(__INT32_TYPE__ argc, char **argv) {
__INT32_TYPE__ xs[main_width] = {1, 2, 3, 4, 5};
float m[3][2] = {};
printf("%zu %d %d %d\n", main_sizes(xs, m), main_last(xs), main_pass_on(xs), main_walk(xs, 3));
memset(xs, 0, sizeof xs);
return argc - 1;
}
