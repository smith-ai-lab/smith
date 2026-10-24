#include <stdint.h>
#include <stdio.h>

typedef f64 Meters;
__INT32_TYPE__ take(struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } t) {
return t[0];
}
__INT32_TYPE__ main() {
struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } a = {1, 2.0f};
struct smith_tuple_i32__f32 { int32_t _0; float _1; } b = a;
struct smith_tuple_i32__f32 { int _0; float _1; } c = b;
struct smith_tuple_i32__f32 { signed _0; float _1; } d = c;
struct smith_tuple_i64__u64__usize { __INT64_TYPE__ _0; __UINT64_TYPE__ _1; __SIZE_TYPE__ _2; } e = {1, 2, 3};
struct smith_tuple_i64__u64__usize { long long _0; unsigned long long _1; size_t _2; } f = e;
struct smith_tuple_const_char_ptr__i8 { const char * _0; __INT8_TYPE__ _1; } g = {"x", 1};
struct smith_tuple_const_char_ptr__i8 { const char * _0; int8_t _1; } h = g;
struct smith_tuple_Meters__f32 { Meters _0; float _1; } m = {1.0, 2.0f};
printf("%d %d %d\n", take(d), take(c), take(b));
printf("%lld %llu %zu %s %d %f\n", f[0], f[1], f[2], h[0], h[1], m[0]);
return 0;
}
