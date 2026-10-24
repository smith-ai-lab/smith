__INT32_TYPE__ shapes_dot(__INT32_TYPE__ n) __asm__("_shapes::dot");
__INT32_TYPE__ shapes_dot(__INT32_TYPE__ n) {
__INT32_TYPE__ buf[4] = {n, n + 1, n + 2, n + 3};struct smith_array_i32_n {
__INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } xs = buf;
__INT32_TYPE__ total = 0;
for (__SIZE_TYPE__ i = 0; i < countof(xs); i++)
total += xs[i] * xs[i];
return total;
}
struct smith_tuple_i32__i32 { __INT32_TYPE__ _0; __INT32_TYPE__ _1; } shapes_pair(__INT32_TYPE__ a) __asm__("_shapes::pair");
struct smith_tuple_i32__i32 { __INT32_TYPE__ _0; __INT32_TYPE__ _1; } shapes_pair(__INT32_TYPE__ a) {
return {a, a * 2};
}
__INT32_TYPE__ shapes_second(__INT32_TYPE__ a) __asm__("_shapes::second");
__INT32_TYPE__ shapes_second(__INT32_TYPE__ a) {
struct smith_tuple_i32__i32 { __INT32_TYPE__ _0; __INT32_TYPE__ _1; } p = shapes_pair(a);
return p[1];
}
