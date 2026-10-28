#include <stdio.h>
#include "pairs.h"
struct smith_struct_Pair_usize {__SIZE_TYPE__ first ;__SIZE_TYPE__ second ; __SIZE_TYPE__ count ; };
typedef struct smith_struct_Pair_usize main_Indexes;struct smith_struct_Pair_f64 {double first ;double second ; __SIZE_TYPE__ count ; };
typedef struct smith_struct_Pair_f64 main_Points;
static __SIZE_TYPE__ main_total(main_Indexes pair) {
return pair.first + pair.second + pair.count;
}
__INT32_TYPE__ main(void) {
main_Indexes a = pairs_both(5, 6);
main_Points b = {.first = 1.5, .second = 2.5, .count = 1};
main_Indexes zero = {};
printf("%zu %g %zu\n", main_total(a), b.first + b.second, main_total(zero));
return 0;
}
