#include <stdio.h>
typedef unsigned u32;
int main() {
__UINT32_TYPE__ a = 3;
int u8 = 4;
int f32 = 5;
printf("%u %d %d\n", a, __UINT8_TYPE__ + 1, float + 1);
return 0;
}
