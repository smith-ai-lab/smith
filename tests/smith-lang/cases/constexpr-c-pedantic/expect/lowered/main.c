#include <stdio.h>

static __INT32_TYPE__ main_fib(__INT32_TYPE__ n) {
return n < 2 ? n : main_fib(n - 1) + main_fib(n - 2);
}
static_assert(main_fib(10) == 55);
enum { main_Fib7 = main_fib(7) };
struct main_Flags {
__UINT32_TYPE__ low : main_fib(4) - 1;
__UINT32_TYPE__ high : 3;
}; [ [ clang :: unavailable ( "main::wider is a function over `type`: it exists only at compile time" ) ] ] void main_wider ( void ) ;
static_assert(main_wider(__UINT8_TYPE__, double) == double);
__INT32_TYPE__ main() {
__INT32_TYPE__ n = 13;
switch (n) {
case main_fib(7):
printf("seven %d\n", (__INT32_TYPE__)main_Fib7);
break;
default:
printf("other %zu\n", sizeof(struct main_Flags));
break;
}
return 0;
}
