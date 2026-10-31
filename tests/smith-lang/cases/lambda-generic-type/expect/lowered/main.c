#include <stdio.h>

typedef __INT32_TYPE__ (*main_Size)(void); [ [ clang :: unavailable ( "main::size_of_type is a generic function: call it from a .smith module" ) ] ] void main_size_of_type ( void ) ;
__INT32_TYPE__ main(void) {
main_Size a =__extension__({extern main_Size main_size_of_type__i16(void)__asm__("_main::size_of_type(__INT16_TYPE__)");main_size_of_type__i16();});
main_Size b =__extension__({extern main_Size main_size_of_type__f64(void)__asm__("_main::size_of_type(double)");main_size_of_type__f64();});
printf("%d %d\n", a(), b());
return 0;
}
static __INT32_TYPE__ main_size_of_type__i16__lambda0(void){return(__INT32_TYPE__)sizeof(__INT16_TYPE__);}extern main_Size main_size_of_type__i16(void)__asm__("_main::size_of_type(__INT16_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]main_Size main_size_of_type__i16(void){return main_size_of_type__i16__lambda0;}
static __INT32_TYPE__ main_size_of_type__f64__lambda0(void){return(__INT32_TYPE__)sizeof(double);}extern main_Size main_size_of_type__f64(void)__asm__("_main::size_of_type(double)");
[[gnu::weak]][[gnu::visibility("hidden")]]main_Size main_size_of_type__f64(void){return main_size_of_type__f64__lambda0;}
