typedef struct { double width; double height; } shapes_area_Rect;
struct shapes_area_Circle { double radius; };
enum shapes_area_Kind { shapes_area_Kind_Square, shapes_area_Kind_Round = 5, shapes_area_Kind_Other };
constexpr __UINT32_TYPE__ shapes_area_sides = 4;
extern __UINT32_TYPE__ shapes_area_calls __asm__("_shapes::area::calls");
__UINT32_TYPE__ shapes_area_calls __asm__("_shapes::area::calls") = 0;
extern double shapes_area_pi __asm__("_shapes::area::pi");
double shapes_area_pi __asm__("_shapes::area::pi") = 3.0;
static __UINT32_TYPE__ shapes_area_private_calls = 0;
static_assert(sizeof(shapes_area_Rect) == 16);
static double shapes_area_half(double value) {
shapes_area_private_calls += 1;
return value / 2;
}
double shapes_area_rect(shapes_area_Rect shape) __asm__("_shapes::area::rect");
double shapes_area_rect(shapes_area_Rect shape) {
shapes_area_calls += 1;
return shape.width * shape.height;
}
double shapes_area_circle(struct shapes_area_Circle shape) __asm__("_shapes::area::circle");
double shapes_area_circle(struct shapes_area_Circle shape) {
shapes_area_calls += 1;
return shapes_area_pi * shape.radius * shape.radius;
}
double shapes_area_triangle(shapes_area_Rect shape) __asm__("_shapes::area::triangle");
double shapes_area_triangle(shapes_area_Rect shape) {
return shapes_area_half(shapes_area_rect(shape));
}
__UINT32_TYPE__ shapes_area_private_count() __asm__("_shapes::area::private_count");
__UINT32_TYPE__ shapes_area_private_count() {
return shapes_area_private_calls;
}
enum shapes_area_Kind shapes_area_kind_of(__UINT32_TYPE__ side_count) __asm__("_shapes::area::kind_of");
enum shapes_area_Kind shapes_area_kind_of(__UINT32_TYPE__ side_count) {
return side_count == shapes_area_sides ? shapes_area_Kind_Square : shapes_area_Kind_Other;
}
