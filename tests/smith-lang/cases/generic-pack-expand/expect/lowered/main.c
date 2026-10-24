#include "expand.h"
#include <stdio.h>

int main(void) {
printf("%d %d\n",smith_call_failed,__extension__({extern __INT32_TYPE__ expand_shifted__(__INT32_TYPE__ off)__asm__("_expand::shifted()");expand_shifted__(10);}));
printf("%d %d %d\n",__extension__({extern __INT32_TYPE__ expand_around__(void)__asm__("_expand::around()");expand_around__();}),smith_call_failed,smith_call_failed);
printf("%d %d\n",__extension__({extern __INT32_TYPE__ expand_rest_empty__(void)__asm__("_expand::rest_empty()");expand_rest_empty__();}),smith_call_failed);
printf("%d %d\n",__extension__({extern __INT32_TYPE__ expand_table__(void)__asm__("_expand::table()");expand_table__();}),smith_call_failed);
printf("%d %d\n",smith_call_failed,__extension__({extern __INT32_TYPE__ expand_over_generic__(void)__asm__("_expand::over_generic()");expand_over_generic__();}));
expand_Pair p = {1, 2};
expand_Pair q = {3, 4};
printf("%d\n",smith_call_failed);
return 0;
}
extern __INT32_TYPE__ expand_shifted__(__INT32_TYPE__ off)__asm__("_expand::shifted()");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ expand_shifted__(__INT32_TYPE__ off){return expand_vsum((__INT32_TYPE__)0);}
extern __INT32_TYPE__ expand_around__(void)__asm__("_expand::around()");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ expand_around__(void){return expand_vsum(2+(__INT32_TYPE__)0,1,3);}
extern __INT32_TYPE__ expand_rest_empty__(void)__asm__("_expand::rest_empty()");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ expand_rest_empty__(void){__INT32_TYPE__ first=expand_vsum(1+(__INT32_TYPE__)0,7);__INT32_TYPE__ last=expand_vsum(1+(__INT32_TYPE__)0,8);__INT32_TYPE__ only=expand_vsum((__INT32_TYPE__)0);return first*10000+last*100+only;}
extern __INT32_TYPE__ expand_table__(void)__asm__("_expand::table()");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ expand_table__(void){__INT32_TYPE__ xs[]={0};__INT32_TYPE__ ys[]={1};__INT32_TYPE__ sum=0;constexpr __SIZE_TYPE__ nx=sizeof xs/sizeof xs[0];constexpr __SIZE_TYPE__ ny=sizeof ys/sizeof ys[0];for(__SIZE_TYPE__ i=0;i<nx;i++)sum+=xs[i];for(__SIZE_TYPE__ i=0;i<ny;i++)sum+=ys[i];return sum*1000+(__INT32_TYPE__)nx*10+(__INT32_TYPE__)ny;}
extern __INT32_TYPE__ expand_over_generic__(void)__asm__("_expand::over_generic()");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ expand_over_generic__(void){return expand_vsum((__INT32_TYPE__)0);}
