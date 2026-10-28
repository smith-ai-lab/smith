#include "finder.h"
#include <stdio.h>
 [ [ clang :: unavailable ( "main::show is a generic function: call it from a .smith module" ) ] ] void main_show ( void ) ;
__INT32_TYPE__ main(void) {
for (__INT32_TYPE__ i = 0; i < 3; i++)__extension__({extern void main_show__I32(__INT32_TYPE__ value)__asm__("_main::show(__INT32_TYPE__)");main_show__I32(i);});
return 0;
}
extern void main_show__I32(__INT32_TYPE__ value)__asm__("_main::show(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]void main_show__I32(__INT32_TYPE__ value){printf("%d\n",(__INT32_TYPE__)value);}
