#include <stdio.h>

struct main_Point {
float x, y;
};
typedef struct main_Point main_Point;
struct main_Node {
struct main_Node *next;
main_Point at;
__INT32_TYPE__ value;
};
typedef struct main_Node main_Node;
union main_Number {
__INT32_TYPE__ whole;
float real;
};
typedef union main_Number main_Number;
enum main_Color { main_red, main_green, main_blue };
typedef enum main_Color main_Color;struct smith_enum_main_Shape {enum main_Shape_tag {main_Shape_tag_circle, main_Shape_tag_empty } tag; union {
main_Point circle; };
} __attribute__((annotate("smith::enum")));
typedef struct smith_enum_main_Shape main_Shape;
struct main_Pending;
typedef struct main_Pending main_Pending;
struct main_Holder {
main_Pending *later;
};
typedef struct main_Holder main_Holder;
struct main_Pending {
__INT32_TYPE__ count;
};
static main_Point main_origin() {
return (main_Point){};
}
static __INT32_TYPE__ main_sum(main_Node *list) {
__INT32_TYPE__ total = 0;
for (main_Node *n = list; n != nullptr; n = n->next)
total += n->value;
return total;
}
typedef struct main_Legacy {
__INT32_TYPE__ a;
} main_Legacy;
typedef struct main_Legacy main_Legacy;
typedef struct {
__INT32_TYPE__ b;
} main_Bare;
__INT32_TYPE__ main() {
main_Point p = main_origin();
main_Node second = {.value = 2};
main_Node first = {.next = &second, .value = 1, .at = p};
main_Number n = {.whole = 7};
main_Color c = main_green;
main_Shape s = {.circle = {.x = 1, .y = 2}};
main_Shape e = .empty;
main_Pending pending = {.count = 4};
main_Holder holder = {.later = &pending};
main_Legacy legacy = {.a = 5};
main_Bare bare = {.b = 6};
struct main_Point q = {.x = 3};
struct Local {
__INT32_TYPE__ inner;
}; typedef struct Local Local;
Local l = {.inner = 8};
printf("%d %d %d %d %d %d %d %d %d %d\n", main_sum(&first), n.whole, (int)c, s == .circle, e == .empty,
holder.later->count, legacy.a, bare.b, (int)q.x, l.inner);
return 0;
}
