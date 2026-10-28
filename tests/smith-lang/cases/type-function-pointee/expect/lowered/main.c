#include <stdio.h>
#include "leaves.h"

typedef struct {
__INT32_TYPE__ a;
double b;
} main_Pair;
int main(void) {
main_Pair pair = {3, 2.5};
main_Pair *pointer = &pair;
__INT32_TYPE__ scalar = 7;
printf("%zu %zu %zu\n", sizeof(__extension__({extern int leaves_first__Pair_P(main_Pair*x)__asm__("_leaves::first(main_Pair *)");leaves_first__Pair_P(pointer);})), sizeof(__extension__({extern int leaves_first__I32(__INT32_TYPE__ x)__asm__("_leaves::first(__INT32_TYPE__)");leaves_first__I32(scalar);})),
sizeof(__extension__({extern int leaves_first__F64_P(double*x)__asm__("_leaves::first(double *)");leaves_first__F64_P(&pair.b);})));
main_Pair *again =__extension__({extern int leaves_first__Pair_P_P(main_Pair**x)__asm__("_leaves::first(main_Pair * *)");leaves_first__Pair_P_P(&pointer);});
main_Pair copy =__extension__({extern int leaves_first__Pair_P(main_Pair*x)__asm__("_leaves::first(main_Pair *)");leaves_first__Pair_P(pointer);});
printf("%d %g %d %d\n", copy.a, copy.b,__extension__({extern int leaves_first__I32(__INT32_TYPE__ x)__asm__("_leaves::first(__INT32_TYPE__)");leaves_first__I32(scalar);}), again->a);
return 0;
}
extern int leaves_first__Pair_P(main_Pair*x)__asm__("_leaves::first(main_Pair *)");
[[gnu::weak]][[gnu::visibility("hidden")]]int leaves_first__Pair_P(main_Pair*x){}
extern int leaves_first__I32(__INT32_TYPE__ x)__asm__("_leaves::first(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]int leaves_first__I32(__INT32_TYPE__ x){}
extern int leaves_first__F64_P(double*x)__asm__("_leaves::first(double *)");
[[gnu::weak]][[gnu::visibility("hidden")]]int leaves_first__F64_P(double*x){}
extern int leaves_first__Pair_P_P(main_Pair**x)__asm__("_leaves::first(main_Pair * *)");
[[gnu::weak]][[gnu::visibility("hidden")]]int leaves_first__Pair_P_P(main_Pair**x){}
