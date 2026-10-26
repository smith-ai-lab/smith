#include <stdio.h>
#include "geo/shapes.h"

extern __INT32_TYPE__ user(void);
__INT32_TYPE__ main() {
geo_shapes_Point p = geo_shapes_make(1, 2);
geo_shapes_Node b = {.value = 3};
geo_shapes_Node a = {.next = &b, .value = 4};
struct geo_shapes_Point q = p;
geo_shapes_Shape s = geo_shapes_round_at(q);
geo_shapes_Color c = geo_shapes_next(geo_shapes_Color_red);
printf("%g %g %d %d %d\n", p.x, p.y, geo_shapes_sum(&a), s == .circle, (int)c);
return user();
}
