#include "math/small.h"

// The header compiles as C++ too (its inline functions are plain C++ then).
extern "C" unsigned cpp_twice(unsigned x) {
    return math_small_twice(x);
}
