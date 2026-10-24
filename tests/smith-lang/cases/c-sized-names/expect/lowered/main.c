#include <stdio.h>
enum { i64 = 7, u8, u16 };
enum Tagged { first, i32 = 40, u64, last };
static int u32(int x) {
return x + 1;
}
static int f64(int x);
static int f64(int x) {
return x * 2;
}
static int local(int x) {
enum { i8 = 3, u8_local };
return x + __INT8_TYPE__ + u8_local;
}
int main() {
int sum = (int)__INT64_TYPE__ + (int)__UINT8_TYPE__ + (int)__UINT16_TYPE__;
printf("%d %d %d %d\n", sum, (int)__INT32_TYPE__, (int)__UINT64_TYPE__, (int)last);
printf("%d %d %d\n", __UINT32_TYPE__(1), double(4), local(1));
return 0;
}
