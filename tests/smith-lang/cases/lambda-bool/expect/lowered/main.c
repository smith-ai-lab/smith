#include <stdio.h>
 [ [ clang :: unavailable ( "main::count_if is a generic function: call it from a .smith module" ) ] ] void main_count_if ( void ) ;
__INT32_TYPE__ main(void) {
printf("%d\n",__extension__({extern __INT32_TYPE__ main_count_if__lambda(__INT32_TYPE__ n)__asm__("_main::count_if(lambda#2c71ecf96b162197)@src/main.smith");main_count_if__lambda(10);}));
return 0;
}
static bool main_count_if__lambda__arg0(__INT32_TYPE__ a){return a%2==0;}_Static_assert(__builtin_types_compatible_p(__typeof__(main_count_if__lambda__arg0),bool(__INT32_TYPE__)),"smith: the argument for `pred` of `count_if` does not have the type of `Lambda(bool (I32))`");extern __INT32_TYPE__ main_count_if__lambda(__INT32_TYPE__ n)__asm__("_main::count_if(lambda#2c71ecf96b162197)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ main_count_if__lambda(__INT32_TYPE__ n){__INT32_TYPE__ total=0;for(__INT32_TYPE__ i=0;i<n;i++){if(main_count_if__lambda__arg0(i))total+=1;}return total;}
