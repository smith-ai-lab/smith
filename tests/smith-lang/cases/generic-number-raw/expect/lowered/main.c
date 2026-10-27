#include <stdio.h>
 [ [ clang :: unavailable ( "main::first is a generic function: call it from a .smith module" ) ] ] void main_first ( void ) ;
__INT32_TYPE__ main(void) {
printf("%d %d\n",__extension__({extern __INT32_TYPE__ main_first__i32_4(__INT32_TYPE__ x)__asm__("_main::first(__INT32_TYPE__, 4)");main_first__i32_4(7);}),__extension__({extern __INT32_TYPE__ main_first__i32_32(__INT32_TYPE__ x)__asm__("_main::first(__INT32_TYPE__, 32)");main_first__i32_32(9);}));
return 0;
}
extern __INT32_TYPE__ main_first__i32_4(__INT32_TYPE__ x)__asm__("_main::first(__INT32_TYPE__, 4)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ main_first__i32_4(__INT32_TYPE__ x){__INT32_TYPE__ cells[4];for(__SIZE_TYPE__ i=0;i<4;i++){cells[i]=x;}return cells[4-1];}
extern __INT32_TYPE__ main_first__i32_32(__INT32_TYPE__ x)__asm__("_main::first(__INT32_TYPE__, 32)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ main_first__i32_32(__INT32_TYPE__ x){__INT32_TYPE__ cells[32];for(__SIZE_TYPE__ i=0;i<32;i++){cells[i]=x;}return cells[32-1];}
