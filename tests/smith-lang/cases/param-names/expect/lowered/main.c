#include "shape/box.h"
#include <stdio.h>

__INT32_TYPE__ main(void) {
shape_box_Box b = {.width = 3, .height = 4};
printf("%d %d %d %d %d %d\n", shape_box_area(&b, 5), shape_box_scaled(2, 3, 4), shape_box_apply(shape_box_width, &b),
shape_box_combined(10), shape_box_height(&b),__extension__({extern __INT32_TYPE__ shape_box_larger__I32(__INT32_TYPE__ width,__INT32_TYPE__ height)__asm__("_shape::box::larger(__INT32_TYPE__)");shape_box_larger__I32(3,9);}));
return 0;
}
extern __INT32_TYPE__ shape_box_larger__I32(__INT32_TYPE__ width,__INT32_TYPE__ height)__asm__("_shape::box::larger(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ shape_box_larger__I32(__INT32_TYPE__ width,__INT32_TYPE__ height){return width>height?width:height;}
