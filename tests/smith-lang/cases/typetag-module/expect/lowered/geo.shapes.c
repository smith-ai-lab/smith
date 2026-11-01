struct geo_shapes_Point {
float x;
float y;
};
typedef struct geo_shapes_Point geo_shapes_Point;
struct geo_shapes_Node {
struct geo_shapes_Node *next;
__INT32_TYPE__ value;
};
typedef struct geo_shapes_Node geo_shapes_Node;
enum geo_shapes_Color { geo_shapes_Color_red, geo_shapes_Color_green };
typedef enum geo_shapes_Color geo_shapes_Color;
struct smith_enum_geo_shapes_Shape {enum geo_shapes_Shape_tag {geo_shapes_Shape_tag_circle, geo_shapes_Shape_tag_empty } tag; union {
geo_shapes_Point circle; };
} __attribute__((annotate("std::enum")));
typedef struct smith_enum_geo_shapes_Shape geo_shapes_Shape;
struct geo_shapes_Hidden {
__INT32_TYPE__ secret;
};
typedef struct geo_shapes_Hidden geo_shapes_Hidden;
geo_shapes_Point geo_shapes_make(float x, float y) __asm__("_geo::shapes::make");
geo_shapes_Point geo_shapes_make(float x, float y) {
geo_shapes_Hidden h = {.secret = 1};
return (geo_shapes_Point){.x = x + (float)h.secret - 1, .y = y};
}
__INT32_TYPE__ geo_shapes_sum(geo_shapes_Node *list) __asm__("_geo::shapes::sum");
__INT32_TYPE__ geo_shapes_sum(geo_shapes_Node *list) {
__INT32_TYPE__ total = 0;
for (geo_shapes_Node *n = list; n != nullptr; n = n->next)
total += n->value;
return total;
}
geo_shapes_Shape geo_shapes_round_at(geo_shapes_Point p) __asm__("_geo::shapes::round_at");
geo_shapes_Shape geo_shapes_round_at(geo_shapes_Point p) {
return {.circle = p};
}
geo_shapes_Color geo_shapes_next(geo_shapes_Color c) __asm__("_geo::shapes::next");
geo_shapes_Color geo_shapes_next(geo_shapes_Color c) {
return c == geo_shapes_Color_red ? geo_shapes_Color_green : geo_shapes_Color_red;
}
