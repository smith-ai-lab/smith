typedef struct { double x; double y; } geo_vec_Vec;
constexpr __UINT32_TYPE__ geo_vec_dimensions = 2;
double geo_vec_length_squared(geo_vec_Vec v) __asm__("_geo.vec.length_squared");
double geo_vec_length_squared(geo_vec_Vec v) { return v.x * v.x + v.y * v.y; }
double geo_vec_dot(geo_vec_Vec a, geo_vec_Vec b) __asm__("_geo.vec.dot");
__UINT32_TYPE__ geo_vec_counter __asm__("_geo.vec.counter") = 5;
