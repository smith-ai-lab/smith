#include <stdio.h>

static __INT32_TYPE__ main_triple(__INT32_TYPE__ x) {
return x * 3;
} [ [ clang :: unavailable ( "main::apply is a generic function: call it from a .smith module" ) ] ] void main_apply ( void ) ;
__INT32_TYPE__ main(void) {
printf("%d\n",__extension__({extern __INT32_TYPE__ main_apply__lambda(__INT32_TYPE__ x)__asm__("_main::apply(lambda#30f0ddf289a7f972)@src/main.smith");main_apply__lambda(4);}));
printf("%d\n",__extension__({extern __INT32_TYPE__ main_apply__lambda_2(__INT32_TYPE__ x)__asm__("_main::apply(lambda#68a4609ef51a7642)@src/main.smith");main_apply__lambda_2(4);}));
printf("%d\n",__extension__({extern __INT32_TYPE__ main_apply__lambda_3(__INT32_TYPE__ x)__asm__("_main::apply(lambda#d4ce5c4567a72c30)@src/main.smith");main_apply__lambda_3(4);}));
printf("%d\n",smith_call_failed);
return 0;
}
static __INT32_TYPE__ main_apply__lambda__arg0(__INT32_TYPE__ a){return a*2;}_Static_assert(__builtin_types_compatible_p(__typeof__(main_apply__lambda__arg0),__INT32_TYPE__(__INT32_TYPE__)),"smith: the argument for `f` of `apply` does not have the type of `lambda(i32 (i32))`");extern __INT32_TYPE__ main_apply__lambda(__INT32_TYPE__ x)__asm__("_main::apply(lambda#30f0ddf289a7f972)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ main_apply__lambda(__INT32_TYPE__ x){return main_apply__lambda__arg0(x)+1;}
static __INT32_TYPE__ main_apply__lambda_2__arg0(__INT32_TYPE__ a){return a*5;}_Static_assert(__builtin_types_compatible_p(__typeof__(main_apply__lambda_2__arg0),__INT32_TYPE__(__INT32_TYPE__)),"smith: the argument for `f` of `apply` does not have the type of `lambda(i32 (i32))`");extern __INT32_TYPE__ main_apply__lambda_2(__INT32_TYPE__ x)__asm__("_main::apply(lambda#68a4609ef51a7642)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ main_apply__lambda_2(__INT32_TYPE__ x){return main_apply__lambda_2__arg0(x)+1;}
static __INT32_TYPE__ main_apply__lambda_3__arg0(__INT32_TYPE__ a){return a+1;}_Static_assert(__builtin_types_compatible_p(__typeof__(main_apply__lambda_3__arg0),__INT32_TYPE__(__INT32_TYPE__)),"smith: the argument for `f` of `apply` does not have the type of `lambda(i32 (i32))`");extern __INT32_TYPE__ main_apply__lambda_3(__INT32_TYPE__ x)__asm__("_main::apply(lambda#d4ce5c4567a72c30)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ main_apply__lambda_3(__INT32_TYPE__ x){return main_apply__lambda_3__arg0(x)+1;}
