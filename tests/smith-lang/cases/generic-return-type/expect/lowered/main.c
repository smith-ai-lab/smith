#include <stdio.h>
 [ [ clang :: unavailable ( "main::by_count is a function over `type`: it exists only at compile time" ) ] ] void main_by_count ( void ) ; [ [ clang :: unavailable ( "main::wide_of is a function over `type`: it exists only at compile time" ) ] ] void main_wide_of ( void ) ; [ [ clang :: unavailable ( "main::from_count is a generic function: call it from a .smith module" ) ] ] void main_from_count ( void ) ; [ [ clang :: unavailable ( "main::widen is a generic function: call it from a .smith module" ) ] ] void main_widen ( void ) ;
int main() {
printf("%zu %zu %zu\n", sizeof(__extension__({extern int main_from_count__u8(void)__asm__("_main::from_count(__UINT8_TYPE__)");main_from_count__u8();})), sizeof(smith_call_failed),
sizeof(smith_call_failed));
printf("%zu %zu\n", sizeof(smith_call_failed), sizeof(smith_call_failed));
return 0;
}
extern int main_from_count__u8(void)__asm__("_main::from_count(__UINT8_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]int main_from_count__u8(void){return 7;}
