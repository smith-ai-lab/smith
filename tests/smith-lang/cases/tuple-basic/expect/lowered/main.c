#include <stdio.h>

typedef struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } Pair;
typedef struct smith_tuple_i32__tuple_f32__f32_end { __INT32_TYPE__ _0; struct smith_tuple_f32__f32 { float _0; float _1; } _1; } Nested;
struct Holder {
struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } pair;
struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } list[2];
};
struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } make(__INT32_TYPE__ a, float b) {
return (struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; }){a, b};
}
Pair braces(__INT32_TYPE__ a, float b) {
return {a, b};
}
Nested nest(void) {
return {1, {2.0f, 3.0f}};
}
__INT32_TYPE__ by_value(struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } t) {
return t[0] + (__INT32_TYPE__)t[1];
}
void by_pointer(Pair *p) {
(*p)[0] += 10;
p->_1 = 0.5f;
}
__INT32_TYPE__ main(void) {
struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } t = {1, 2.0f};
t[1] = 2.5f;
Pair p = make(3, 4.5f);
Pair q = braces(5, 6.0f);
Nested n = nest();
struct Holder h = {{7, 1.5f}, {{1, 1.0f}, {2, 2.0f}}};
h.list[1][0] = 9;
struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } list[2] = {{1, 1.5f}, {2, 2.5f}};
by_pointer(&p);
printf("%d %f\n", t[0], (double)t[1]);
printf("%d %f\n", p[0], (double)p[1]);
printf("%d %d\n", q[0], by_value(q));
printf("%d %f %f\n", n[0], (double)n[1][0], (double)n[1][1]);
printf("%d %d\n", h.pair[0], h.list[1][0]);
printf("%d %f\n", list[1][0], (double)list[1][1]);
printf("%d %d %d\n", (__INT32_TYPE__)countof(t), (__INT32_TYPE__)countof(n), (__INT32_TYPE__)countof(n[1]));
printf("%d\n", (__INT32_TYPE__)(sizeof(struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; }) == sizeof t));
return 0;
}
