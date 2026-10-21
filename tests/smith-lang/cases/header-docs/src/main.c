#include <stdio.h>

#include "geo/shape.h"

uint32_t geo_shape_seven(void) {
    return 7;
}

int main(void) {
    geo_shape_Point p = {3, 4, 0};
    printf("%llu %d %d %u\n", (unsigned long long)geo_shape_norm2(p), geo_is_origin(p), geo_is_origin((geo_shape_Point){0}),
           geo_shape_origin_count);
    return 0;
}
