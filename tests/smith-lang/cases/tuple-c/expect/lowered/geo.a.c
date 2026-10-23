typedef struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } geo_a_Pair;
struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } geo_a_make(__INT32_TYPE__ a, float b) __asm__("_geo::a::make");
struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } geo_a_make(__INT32_TYPE__ a, float b) {
return {a, b};
}
__INT32_TYPE__ geo_a_sum(struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } t) __asm__("_geo::a::sum");
__INT32_TYPE__ geo_a_sum(struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } t) {
return t[0] + (__INT32_TYPE__)t[1];
}
geo_a_Pair geo_a_swap(geo_a_Pair p, struct smith_tuple_f32__i32 { float _0; __INT32_TYPE__ _1; } *other) __asm__("_geo::a::swap");
geo_a_Pair geo_a_swap(geo_a_Pair p, struct smith_tuple_f32__i32 { float _0; __INT32_TYPE__ _1; } *other) {
(*other)[0] = (float)p[0];
(*other)[1] = (__INT32_TYPE__)p[1];
return {(__INT32_TYPE__)p[1], (float)p[0]};
}
