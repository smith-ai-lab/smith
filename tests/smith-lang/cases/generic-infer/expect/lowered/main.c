#include "pick.h"
#include <stdio.h>
typedef struct {
__INT32_TYPE__ x;
__INT32_TYPE__ y;
} Point;
static double half(double v) {
return v / 2;
}
int main(void) {
__INT32_TYPE__ a = 3;
__INT32_TYPE__ b = 8;
double d = 2.5;
Point p = {10, 20};
Point *pp = &p;
__INT32_TYPE__ numbers[3] = {7, 8, 9};
__UINT8_TYPE__ byte = 200;
const __INT32_TYPE__ fixed = 4;
printf("%d %g\n",__extension__({extern int pick_larger__int(int a,int b)__asm__("_pick::larger(int)");pick_larger__int(a,b);}),__extension__({extern int pick_larger__int(int a,int b)__asm__("_pick::larger(int)");pick_larger__int(d,1.5);}));
printf("%d %d %d\n",__extension__({extern int pick_larger__int(int a,int b)__asm__("_pick::larger(int)");pick_larger__int(p.x,pp->y);}),__extension__({extern int pick_larger__int(int a,int b)__asm__("_pick::larger(int)");pick_larger__int(p.x,3);}),__extension__({extern int pick_larger__int(int a,int b)__asm__("_pick::larger(int)");pick_larger__int(numbers[1],fixed);}));
printf("%d %g %zu\n",__extension__({extern int pick_larger__int(int a,int b)__asm__("_pick::larger(int)");pick_larger__int(a,100);}),__extension__({extern int pick_larger__int(int a,int b)__asm__("_pick::larger(int)");pick_larger__int(half(9),d);}),__extension__({extern __SIZE_TYPE__ pick_size_of_value__int(int value)__asm__("_pick::size_of_value(int)");pick_size_of_value__int(byte);}));
printf("%d %zu\n",__extension__({extern int pick_first_of__int(const int*values,__SIZE_TYPE__ count)__asm__("_pick::first_of(int)");pick_first_of__int(numbers,3);}),__extension__({extern __SIZE_TYPE__ pick_size_of_value__int(int value)__asm__("_pick::size_of_value(int)");pick_size_of_value__int((__UINT16_TYPE__)3);}));
__INT32_TYPE__ slot = 0;__extension__({extern void pick_set__int(int*out,int value)__asm__("_pick::set(int)");pick_set__int(&slot,42);});
printf("%d %d\n", slot,__extension__({extern __INT32_TYPE__ pick_twice__i32(__INT32_TYPE__ value)__asm__("_pick::twice(__INT32_TYPE__)");pick_twice__i32(slot);}));
__UINT64_TYPE__ big = 5;
printf("%d %g %d %d\n",__extension__({extern int pick_larger__int(int a,int b)__asm__("_pick::larger(int)");pick_larger__int(a+1,b*2);}),__extension__({extern int pick_larger__int(int a,int b)__asm__("_pick::larger(int)");pick_larger__int(d*2,d+1);}),__extension__({extern int pick_larger__int(int a,int b)__asm__("_pick::larger(int)");pick_larger__int(a,1<<3);}),__extension__({extern int pick_larger__int(int a,int b)__asm__("_pick::larger(int)");pick_larger__int(a>2?a:b,5);}));
printf("%llu %d\n",__extension__({extern int pick_larger__int(int a,int b)__asm__("_pick::larger(int)");pick_larger__int(big+a,2+2);}),__extension__({extern int pick_larger__int(int a,int b)__asm__("_pick::larger(int)");pick_larger__int((__INT32_TYPE__)big-1,a);}));
return 0;
}
extern int pick_larger__int(int a,int b)__asm__("_pick::larger(int)");
[[gnu::weak]][[gnu::visibility("hidden")]]int pick_larger__int(int a,int b){return a>b?a:b;}
extern __SIZE_TYPE__ pick_size_of_value__int(int value)__asm__("_pick::size_of_value(int)");
[[gnu::weak]][[gnu::visibility("hidden")]]__SIZE_TYPE__ pick_size_of_value__int(int value){(void)value;return sizeof(int);}
extern int pick_first_of__int(const int*values,__SIZE_TYPE__ count)__asm__("_pick::first_of(int)");
[[gnu::weak]][[gnu::visibility("hidden")]]int pick_first_of__int(const int*values,__SIZE_TYPE__ count){return count>0?values[0]:(int){0};}
extern void pick_set__int(int*out,int value)__asm__("_pick::set(int)");
[[gnu::weak]][[gnu::visibility("hidden")]]void pick_set__int(int*out,int value){*out=value;}
extern __INT32_TYPE__ pick_twice__i32(__INT32_TYPE__ value)__asm__("_pick::twice(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ pick_twice__i32(__INT32_TYPE__ value){return __extension__({extern __INT32_TYPE__ pick_larger__i32(__INT32_TYPE__ a,__INT32_TYPE__ b)__asm__("_pick::larger(__INT32_TYPE__)");pick_larger__i32(value,value);});}
extern __INT32_TYPE__ pick_larger__i32(__INT32_TYPE__ a,__INT32_TYPE__ b)__asm__("_pick::larger(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ pick_larger__i32(__INT32_TYPE__ a,__INT32_TYPE__ b){return a>b?a:b;}
