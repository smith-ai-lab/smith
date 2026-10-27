#include "count.h"

typedef struct {
__INT32_TYPE__ a;
float b;
} main_Pair;
__INT32_TYPE__ main(void) {__extension__({extern void count_show__Pair(void)__asm__("_count::show(main_Pair)");count_show__Pair();});
return (__INT32_TYPE__)__extension__({extern __SIZE_TYPE__ count_count_of__Pair(void)__asm__("_count::count_of(main_Pair)");count_count_of__Pair();}) - 2;
}
extern void count_show__Pair(void)__asm__("_count::show(main_Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]]void count_show__Pair(void){printf("%zu %s\n",smith_call_failed,smith_call_failed.name);}
extern __SIZE_TYPE__ count_count_of__Pair(void)__asm__("_count::count_of(main_Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]]__SIZE_TYPE__ count_count_of__Pair(void){return smith_call_failed;}
