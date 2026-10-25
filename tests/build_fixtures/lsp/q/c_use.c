#include "geom/vec.h"

float c_use(geom_vec_Vec a) {
    return geom_vec_dot(a, a);
}

int main(void) {
    return 0;
}
