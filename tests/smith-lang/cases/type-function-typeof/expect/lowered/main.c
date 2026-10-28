#include <stdio.h>
 [ [ clang :: unavailable ( "main::element is a function over `type`: it exists only at compile time" ) ] ] void main_element ( void ) ;
int main(void) {
constexpr type once = main_element(double);
constexpr type small = main_element(__UINT8_TYPE__);
once x = 1.5;
small y = 7;
printf("%zu %zu\n", sizeof(x), sizeof(y));
return 0;
}
