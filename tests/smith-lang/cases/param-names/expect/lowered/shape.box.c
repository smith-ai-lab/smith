typedef struct {
__INT32_TYPE__ width;
__INT32_TYPE__ height;
} shape_box_Box;
__INT32_TYPE__ shape_box_width(shape_box_Box *box) __asm__("_shape::box::width");
__INT32_TYPE__ shape_box_height(shape_box_Box *box) __asm__("_shape::box::height");
__INT32_TYPE__ shape_box_height(shape_box_Box *box) { return box->height; }
__INT32_TYPE__ shape_box_area(shape_box_Box *width, __INT32_TYPE__ height) __asm__("_shape::box::area");
__INT32_TYPE__ shape_box_area(shape_box_Box *width, __INT32_TYPE__ height) { return width->width * height; }
__INT32_TYPE__ shape_box_scaled(__INT32_TYPE__ area, __INT32_TYPE__ width, __INT32_TYPE__ height) __asm__("_shape::box::scaled");
__INT32_TYPE__ shape_box_scaled(__INT32_TYPE__ area, __INT32_TYPE__ width, __INT32_TYPE__ height) { return area * width + height; }
__INT32_TYPE__ shape_box_apply(__INT32_TYPE__ (*width)(shape_box_Box *box), shape_box_Box *box) __asm__("_shape::box::apply");
__INT32_TYPE__ shape_box_apply(__INT32_TYPE__ (*width)(shape_box_Box *box), shape_box_Box *box) { return width(box); }
static __INT32_TYPE__ shape_box_helper(__INT32_TYPE__ area, __INT32_TYPE__ height);
static __INT32_TYPE__ shape_box_helper(__INT32_TYPE__ area, __INT32_TYPE__ height) { return area + height + shape_box_width(&(shape_box_Box){.width = 1, .height = 2}); }
__INT32_TYPE__ shape_box_combined(__INT32_TYPE__ x) __asm__("_shape::box::combined");
__INT32_TYPE__ shape_box_combined(__INT32_TYPE__ x) { return shape_box_helper(x, shape_box_height(&(shape_box_Box){.width = 1, .height = 2})); }
__INT32_TYPE__ shape_box_width(shape_box_Box *box) __asm__("_shape::box::width");
__INT32_TYPE__ shape_box_width(shape_box_Box *box) { return box->width; } [ [ clang :: unavailable ( "shape::box::larger is a generic function: call it from a .smith module" ) ] ] void shape_box_larger ( void ) ;
