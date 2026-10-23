typedef double geo_a_Meters;
typedef struct smith_tuple_geo_a_Meters__f32 { geo_a_Meters _0; float _1; } geo_a_Reach;
struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } geo_a_make(__INT32_TYPE__ a, float b) __asm__("_geo::a::make");
struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } geo_a_make(__INT32_TYPE__ a, float b) {
return {a, b};
}
__INT32_TYPE__ geo_a_sum(struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } t) __asm__("_geo::a::sum");
__INT32_TYPE__ geo_a_sum(struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } t) {
return t[0] + (__INT32_TYPE__)t[1];
}
geo_a_Reach geo_a_reach(geo_a_Meters m) __asm__("_geo::a::reach");
geo_a_Reach geo_a_reach(geo_a_Meters m) {
geo_a_Reach r = {m, 1.0f};
return r;
}
struct smith_tuple_geo_a_Meters__f32 { geo_a_Meters _0; float _1; } geo_a_reach2(geo_a_Meters m) __asm__("_geo::a::reach2");
struct smith_tuple_geo_a_Meters__f32 { geo_a_Meters _0; float _1; } geo_a_reach2(geo_a_Meters m) {
return {m, 2.0f};
}
void geo_a_scale(struct smith_tuple_geo_a_Meters__f32 { geo_a_Meters _0; float _1; } *t, double by) __asm__("_geo::a::scale");
void geo_a_scale(struct smith_tuple_geo_a_Meters__f32 { geo_a_Meters _0; float _1; } *t, double by) {
(*t)[0] *= by;
}
