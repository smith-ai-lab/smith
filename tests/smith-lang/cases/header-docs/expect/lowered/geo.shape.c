#include <stdint.h>

typedef struct {
__UINT32_TYPE__ x;
__UINT32_TYPE__ y;
__UINT32_TYPE__ z;
} geo_shape_Point;
__UINT64_TYPE__ geo_shape_norm2(geo_shape_Point p) __asm__("_geo::shape::norm2");
__UINT64_TYPE__ geo_shape_norm2(geo_shape_Point p) {
return (__UINT64_TYPE__)p.x * p.x + (__UINT64_TYPE__)p.y * p.y + (__UINT64_TYPE__)p.z * p.z;
}
extern __UINT32_TYPE__ geo_shape_origin_count __asm__("_geo::shape::origin_count");
__UINT32_TYPE__ geo_shape_origin_count __asm__("_geo::shape::origin_count") = 1;
bool geo_is_origin(geo_shape_Point p);
bool geo_is_origin(geo_shape_Point p) {
return geo_shape_norm2(p) == 0;
}
static __UINT32_TYPE__ geo_shape_helper(void) {
return 7;
}
__UINT32_TYPE__ geo_shape_seven(void) __asm__("_geo::shape::seven");
