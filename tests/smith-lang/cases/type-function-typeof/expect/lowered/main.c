#include <stdio.h>
 [ [ clang :: unavailable ( "main::Element is a function over `Type`: it exists only at compile time" ) ] ] void main_Element ( void ) ;
int main(void) {
constexpr Type Once = main_Element(double);
constexpr Type Small = main_Element(__UINT8_TYPE__);
Once x = 1.5;
Small y = 7;
printf("%zu %zu\n", sizeof(x), sizeof(y));
return 0;
}
