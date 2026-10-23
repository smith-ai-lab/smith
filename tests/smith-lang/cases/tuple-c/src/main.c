// C includes the module's header and uses its tuples: the struct has the derived name and the fields _0, _1.
#include <stdio.h>

#include "geo/a.h"

int main(void) {
    struct smith_tuple_i32__f32 p = geo_a_make(2, 3.5f);
    struct smith_tuple_f32__i32 other = {0};
    geo_a_Pair swapped = geo_a_swap(p, &other);
    printf("%d %d %d %f %d\n", p._0, geo_a_sum(p), swapped._0, (double)other._0, other._1);
    return 0;
}
