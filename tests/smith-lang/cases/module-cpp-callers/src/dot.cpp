#include "geo/vec.h"

extern "C" double geo_vec_dot(geo_vec_Vec a, geo_vec_Vec b) {
    return a.x * b.x + a.y * b.y;
}
