#include <stdio.h>

typedef __INT32_TYPE__ (*main_Call)(__INT32_TYPE__);
static __INT32_TYPE__ main_twice(__INT32_TYPE__ x) {
return x * 2;
}
static __INT32_TYPE__ main_negate(__INT32_TYPE__ x) {
return -x;
} [ [ clang :: unavailable ( "main::wrap is a generic function: call it from a .smith module" ) ] ] void main_wrap ( void ) ;
__INT32_TYPE__ main(void) {
main_Call a =smith_call_failed;
main_Call b =smith_call_failed;
printf("%d %d\n", a(10), b(10));
return 0;
}
