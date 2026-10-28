#include <stdio.h>
 [ [ clang :: unavailable ( "main::twice is a function over `type`: it exists only at compile time" ) ] ] void main_twice ( void ) ;
int main(void) {
constexpr type once = main_twice(__INT32_TYPE__ *);
constexpr type deep = main_twice(main_twice(__INT32_TYPE__ **));
once x = 1;
deep y = 2;
printf("%d %d\n", x, y);
return 0;
}
