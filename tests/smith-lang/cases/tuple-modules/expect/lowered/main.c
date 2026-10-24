#include "geo/a.h"
#include "geo/b.h"
#include <stdio.h>

__INT32_TYPE__ main() {
auto p = geo_a_make(2, 3.0f);
struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } q = p;
auto m = geo_b_mk();
geo_a_Reach r = geo_a_reach(4.0);
auto longer = geo_b_longer(r);
printf("%d %d %d\n", geo_b_twice(q), geo_a_sum(p), (__INT32_TYPE__)countof(p));
printf("%f %f %f\n", m[0], r[0], longer[0]);
return 0;
}
