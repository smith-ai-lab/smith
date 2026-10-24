#include "geo/a.h"

__INT32_TYPE__ geo_b_twice(struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } t) __asm__("_geo::b::twice");
__INT32_TYPE__ geo_b_twice(struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } t) {
return geo_a_sum(t) * 2;
}
struct smith_tuple_geo_a_Meters__f32 { geo_a_Meters _0; float _1; } geo_b_mk() __asm__("_geo::b::mk");
struct smith_tuple_geo_a_Meters__f32 { geo_a_Meters _0; float _1; } geo_b_mk() {
return geo_a_reach2(5.0);
}
geo_a_Reach geo_b_longer(geo_a_Reach r) __asm__("_geo::b::longer");
geo_a_Reach geo_b_longer(geo_a_Reach r) {
struct smith_tuple_geo_a_Meters__f32 { geo_a_Meters _0; float _1; } copy = r;
geo_a_scale(&copy, 2.0);
return copy;
}
