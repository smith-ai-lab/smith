#include <stdio.h>

#include "geo/shapes.h"

// C names the types with the typedefs the header carries, or with the keyword.
int user(void) {
    geo_shapes_Point p = geo_shapes_make(5, 6);
    struct geo_shapes_Node n = {.next = NULL, .value = 9};
    geo_shapes_Color c = geo_shapes_next(geo_shapes_Color_red);
    printf("%g %g %d %d\n", (double)p.x, (double)p.y, (int)geo_shapes_sum(&n), (int)c);
    return 0;
}
