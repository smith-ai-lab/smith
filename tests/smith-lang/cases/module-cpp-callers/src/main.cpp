#include <cstdio>

#include "geo/vec.h"

int main() {
    geo_vec_Vec v{3.0, 4.0};
    std::printf("%.1f %.1f %u %u\n", geo_vec_length_squared(v), geo_vec_dot(v, v), geo_vec_dimensions, geo_vec_counter);
    return 0;
}
