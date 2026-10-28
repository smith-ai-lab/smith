#include <stdio.h>
 [ [ clang :: unavailable ( "main::Twice is a function over `Type`: it exists only at compile time" ) ] ] void main_Twice ( void ) ;
int main(void) {
constexpr Type Once = main_Twice(__INT32_TYPE__ *);
constexpr Type Deep = main_Twice(main_Twice(__INT32_TYPE__ **));
Once x = 1;
Deep y = 2;
printf("%d %d\n", x, y);
return 0;
}
