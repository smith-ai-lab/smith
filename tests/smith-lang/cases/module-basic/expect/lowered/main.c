#include "shapes/area.h"
#include <stdio.h>
int main(void) {
shapes_area_Rect r = {3.0, 4.0};
struct shapes_area_Circle c = {2.0};
printf("%.1f %.1f %.1f\n", shapes_area_rect(r), shapes_area_circle(c), shapes_area_triangle(r));
printf("%u %u %d %d %d\n", shapes_area_calls, shapes_area_private_count(), shapes_area_Square, shapes_area_Round, shapes_area_Other);
printf("%d %u\n", shapes_area_kind_of(4) == shapes_area_Square, shapes_area_sides);
return 0;
}
