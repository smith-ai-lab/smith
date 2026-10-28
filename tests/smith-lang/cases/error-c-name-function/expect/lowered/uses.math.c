#include <math.h>

float uses_math_root(float value) __asm__("_uses::math::root");
float uses_math_root(float value) {
return sqrtf(value);
}
float uses_math_root_ok(float value) __asm__("_uses::math::root_ok");
float uses_math_root_ok(float value) {
return sqrtf(value);
}
