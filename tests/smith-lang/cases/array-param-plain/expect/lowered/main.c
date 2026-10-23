#include <stdio.h>

__INT32_TYPE__ unsized(__INT32_TYPE__ xs[], __SIZE_TYPE__ count) {
__INT32_TYPE__ total = 0;
for (__SIZE_TYPE__ i = 0; i < count; i++) {
total += xs[i];
}
return total;
}
__INT32_TYPE__ moves(__INT32_TYPE__ xs[2]) {
xs++;
return xs[0] + xs[2];
}
__SIZE_TYPE__ size(__INT32_TYPE__ xs[4]) {
return sizeof(xs);
}
__INT32_TYPE__ main(__INT32_TYPE__ argc, char *argv[]) {
__INT32_TYPE__ xs[4] = {1, 2, 3, 4};
__INT32_TYPE__ *p = xs;
printf("%d %d %zu\n", unsized(xs, 4), moves(p), size(p));
return 0;
}
