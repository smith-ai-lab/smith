#include "num.h"

typedef struct {
__INT32_TYPE__ x;
__INT32_TYPE__ y;
} main_Point;
int main(void) {
__INT32_TYPE__ three =__extension__({extern __INT32_TYPE__ num_add__I32(__INT32_TYPE__ a,__INT32_TYPE__ b)__asm__("_num::add(__INT32_TYPE__)");num_add__I32(1,2);});
main_Point a = {1, 2};
main_Point b =__extension__({extern main_Point num_add__Point(main_Point a,main_Point b)__asm__("_num::add(main_Point)");num_add__Point(a,a);});
return three + b.x;
}
extern __INT32_TYPE__ num_add__I32(__INT32_TYPE__ a,__INT32_TYPE__ b)__asm__("_num::add(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ num_add__I32(__INT32_TYPE__ a,__INT32_TYPE__ b){return a+b;}
extern main_Point num_add__Point(main_Point a,main_Point b)__asm__("_num::add(main_Point)");
[[gnu::weak]][[gnu::visibility("hidden")]]main_Point num_add__Point(main_Point a,main_Point b){return a+b;}
