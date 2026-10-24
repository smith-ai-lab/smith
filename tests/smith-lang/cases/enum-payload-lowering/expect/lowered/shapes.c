typedef struct {
float radius;
} shapes_Circle;
typedef struct {
float w;
float h;
} shapes_Rect;
typedef struct shapes_Shape {enum shapes_Shape_tag {shapes_Shape_tag_circle, shapes_Shape_tag_rect, shapes_Shape_tag_inline_one, shapes_Shape_tag_empty } tag; union {
shapes_Circle circle;
shapes_Rect rect;
struct { __INT32_TYPE__ a; } inline_one; };
} __attribute__((annotate("smith::enum"))) shapes_Shape;
typedef struct shapes_Mark {enum shapes_Mark_tag {shapes_Mark_tag_none, shapes_Mark_tag_some } tag;
} __attribute__((annotate("smith::enum"))) shapes_Mark;
float shapes_area(shapes_Shape s) __asm__("_shapes::area");
float shapes_area(shapes_Shape s) {
switch (s) {
case .circle: return s.circle.radius * s.circle.radius * 3;
case .rect: return s.rect.w * s.rect.h;
case .inline_one: return (float)s.inline_one.a;
case .empty: return 0;
}
return -1;
}
shapes_Shape shapes_unit(void) __asm__("_shapes::unit");
shapes_Shape shapes_unit(void) {
return {.circle = {.radius = 1}};
}
bool shapes_is_empty(shapes_Shape s) __asm__("_shapes::is_empty");
bool shapes_is_empty(shapes_Shape s) {
return s == .empty;
}
