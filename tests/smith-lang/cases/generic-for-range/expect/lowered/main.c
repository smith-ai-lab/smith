#include <stdio.h>
#include "reflect.h"

typedef struct {
__INT32_TYPE__ m;
float b;
} main_Pair;
typedef struct {
main_Pair pair;
__INT32_TYPE__ count;
double scale;
} main_Mixed;
__INT32_TYPE__ main(void) {
main_Pair pair = {1, 2.5f};
main_Mixed mixed = {pair, 5, 0.5};__extension__({extern void reflect_show__Pair(main_Pair*x)__asm__("_reflect::show(main_Pair)");reflect_show__Pair(&pair);});__extension__({extern void reflect_show__Mixed(main_Mixed*x)__asm__("_reflect::show(main_Mixed)");reflect_show__Mixed(&mixed);});
printf("%g %g\n",__extension__({extern double reflect_sum__Pair(main_Pair value)__asm__("_reflect::sum(main_Pair)");reflect_sum__Pair(pair);}),__extension__({extern double reflect_sum__Mixed(main_Mixed value)__asm__("_reflect::sum(main_Mixed)");reflect_sum__Mixed(mixed);}));
printf("%d %d\n",smith_call_failed,smith_call_failed);smith_call_failed;
printf("%d\n",smith_call_failed);
return 0;
}
extern void reflect_show__Pair(main_Pair*x)__asm__("_reflect::show(main_Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]]void reflect_show__Pair(main_Pair*x){printf("%zu fields:",(__SIZE_TYPE__)smith_call_failed);printf("\n");}
extern void reflect_show__Mixed(main_Mixed*x)__asm__("_reflect::show(main_Mixed)");
[[gnu::weak]][[gnu::visibility("hidden")]]void reflect_show__Mixed(main_Mixed*x){printf("%zu fields:",(__SIZE_TYPE__)smith_call_failed);printf("\n");}
extern double reflect_sum__Pair(main_Pair value)__asm__("_reflect::sum(main_Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]]double reflect_sum__Pair(main_Pair value){double total=0;return total;}
extern double reflect_sum__Mixed(main_Mixed value)__asm__("_reflect::sum(main_Mixed)");
[[gnu::weak]][[gnu::visibility("hidden")]]double reflect_sum__Mixed(main_Mixed value){double total=0;return total;}
