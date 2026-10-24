#include "kind.h"
#include <stdio.h>
typedef struct {
__INT32_TYPE__ x;
__INT32_TYPE__ y;
} Point;
int main() {
float f = 1.5f;
__UINT64_TYPE__ big = 9;
__INT32_TYPE__ small = -3;
__UINT8_TYPE__ byte = 7;
Point p = {10, 20};
printf("%d %d %d %d %d\n",__extension__({extern __INT32_TYPE__ kind_describe__f32(float value)__asm__("_kind::describe(float)");kind_describe__f32(f);}),__extension__({extern __INT32_TYPE__ kind_describe__u64(__UINT64_TYPE__ value)__asm__("_kind::describe(__UINT64_TYPE__)");kind_describe__u64(big);}),__extension__({extern __INT32_TYPE__ kind_describe__i32_P(__INT32_TYPE__*value)__asm__("_kind::describe(__INT32_TYPE__ *)");kind_describe__i32_P(&small);}),__extension__({extern __INT32_TYPE__ kind_describe__u8(__UINT8_TYPE__ value)__asm__("_kind::describe(__UINT8_TYPE__)");kind_describe__u8(byte);}),__extension__({extern __INT32_TYPE__ kind_describe__Point(Point value)__asm__("_kind::describe(Point)");kind_describe__Point(p);}));
printf("%d %d %d\n",__extension__({extern __INT32_TYPE__ kind_describe__i32(__INT32_TYPE__ value)__asm__("_kind::describe(__INT32_TYPE__)");kind_describe__i32(small);}),__extension__({extern __INT32_TYPE__ kind_floats_only__f32(float value)__asm__("_kind::floats_only(float)");kind_floats_only__f32(f);}),__extension__({extern __INT32_TYPE__ kind_floats_only__i32(__INT32_TYPE__ value)__asm__("_kind::floats_only(__INT32_TYPE__)");kind_floats_only__i32(small);}));
printf("%d %d %d\n",__extension__({extern __INT32_TYPE__ kind_bytes__u8(void)__asm__("_kind::bytes(__UINT8_TYPE__)");kind_bytes__u8();}),__extension__({extern __INT32_TYPE__ kind_bytes__u64(void)__asm__("_kind::bytes(__UINT64_TYPE__)");kind_bytes__u64();}),__extension__({extern __INT32_TYPE__ kind_bytes__Point(void)__asm__("_kind::bytes(Point)");kind_bytes__Point();}));
return 0;
}
extern __INT32_TYPE__ kind_describe__f32(float value)__asm__("_kind::describe(float)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ kind_describe__f32(float value){}
extern __INT32_TYPE__ kind_describe__u64(__UINT64_TYPE__ value)__asm__("_kind::describe(__UINT64_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ kind_describe__u64(__UINT64_TYPE__ value){}
extern __INT32_TYPE__ kind_describe__i32_P(__INT32_TYPE__*value)__asm__("_kind::describe(__INT32_TYPE__ *)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ kind_describe__i32_P(__INT32_TYPE__*value){}
extern __INT32_TYPE__ kind_describe__u8(__UINT8_TYPE__ value)__asm__("_kind::describe(__UINT8_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ kind_describe__u8(__UINT8_TYPE__ value){}
extern __INT32_TYPE__ kind_describe__Point(Point value)__asm__("_kind::describe(Point)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ kind_describe__Point(Point value){}
extern __INT32_TYPE__ kind_describe__i32(__INT32_TYPE__ value)__asm__("_kind::describe(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ kind_describe__i32(__INT32_TYPE__ value){}
extern __INT32_TYPE__ kind_floats_only__f32(float value)__asm__("_kind::floats_only(float)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ kind_floats_only__f32(float value){return 0;}
extern __INT32_TYPE__ kind_floats_only__i32(__INT32_TYPE__ value)__asm__("_kind::floats_only(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ kind_floats_only__i32(__INT32_TYPE__ value){return 0;}
extern __INT32_TYPE__ kind_bytes__u8(void)__asm__("_kind::bytes(__UINT8_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ kind_bytes__u8(void){return 3+(sizeof(__UINT8_TYPE__)!=8);}
extern __INT32_TYPE__ kind_bytes__u64(void)__asm__("_kind::bytes(__UINT64_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ kind_bytes__u64(void){return 3+(sizeof(__UINT64_TYPE__)!=8);}
extern __INT32_TYPE__ kind_bytes__Point(void)__asm__("_kind::bytes(Point)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ kind_bytes__Point(void){return 3+(sizeof(Point)!=8);}
