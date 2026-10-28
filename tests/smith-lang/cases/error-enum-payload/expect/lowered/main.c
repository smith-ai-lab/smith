#include <stdio.h>

typedef struct {
float radius;
} main_Circle;
typedef struct smith_enum_main_Shape {enum main_Shape_tag {main_Shape_tag_circle, main_Shape_tag_empty } tag; union {
main_Circle circle; };
} __attribute__((annotate("smith::enum"))) main_Shape;
static __INT32_TYPE__ main_describe(main_Shape s) {
switch (s) {
case .circle: return 1;
default: return 0;
}
}
__INT32_TYPE__ main(void) {
main_Shape a = .circle;
main_Shape b = .nothing;
main_Shape c = .empty;
printf("%d\n", 0);
return main_describe(a) + main_describe(b) + (__INT32_TYPE__)(c == .empty) + main_describe(.empty);
}
static void main_nowhere() {
ready(.empty);
}
