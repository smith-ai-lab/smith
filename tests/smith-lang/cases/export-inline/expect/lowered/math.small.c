static inline __UINT32_TYPE__ math_small_twice(__UINT32_TYPE__ x) {
return x * 2;
}
static inline __UINT32_TYPE__ math_small_clamp(__UINT32_TYPE__ x, __UINT32_TYPE__ low, __UINT32_TYPE__ high) {
if (x < low) return low;
if (x > high) return high;
return x;
}
static inline __UINT32_TYPE__ math_small_clamp_twice(__UINT32_TYPE__ x) {
return math_small_clamp(math_small_twice(x), 10, 100);
}
__UINT32_TYPE__ math_small_regular(__UINT32_TYPE__ x) __asm__("_math::small::regular");
__UINT32_TYPE__ math_small_regular(__UINT32_TYPE__ x) {
return math_small_twice(x) + 1;
}
