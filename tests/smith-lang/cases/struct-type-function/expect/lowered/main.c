#include <stdio.h>
struct smith_struct_Pair_usize {__SIZE_TYPE__ first;__SIZE_TYPE__ second;
__SIZE_TYPE__ count; };
typedef struct smith_struct_Pair_usize main_Indexes;struct smith_struct_Pair_f64 {double first;double second;
__SIZE_TYPE__ count; };
typedef struct smith_struct_Pair_f64 main_Points;
static __SIZE_TYPE__ main_total(main_Indexes pair) {
return pair.first + pair.second + pair.count;
}
static double main_mid(main_Points pair) {
return (pair.first + pair.second) / 2;
}
__INT32_TYPE__ main(void) {
main_Indexes a = {.first = 3, .second = 4, .count = 2};
main_Points b = {.first = 1.5, .second = 2.5};
main_Indexes zero = {};
printf("%zu %g %zu\n", main_total(a), main_mid(b), main_total(zero));
return 0;
}
