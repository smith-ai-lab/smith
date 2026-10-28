#include <stdio.h>

__INT32_TYPE__ main() {
struct smith_tuple_i32__f32 { __INT32_TYPE__ _0; float _1; } t = {1, 2.0f};
__INT32_TYPE__ i = 1;
printf("%d\n", t[i]);
printf("%d\n", t[2]);
printf("%d\n", t[i + 1]);
return t[1] > 0;
}
