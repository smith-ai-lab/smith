#include <stdio.h>

static type main_by_count(type T, __SIZE_TYPE__ k) {
return k == 0 ? T : k == 1 ? __INT16_TYPE__ : double;
}
static type main_wide_of(type T) {
return sizeof(T) <= 2 ? __INT64_TYPE__ : T;
} [ [ clang :: unavailable ( "main::from_count is a generic function: call it from a .smith module" ) ] ] void main_from_count ( void ) ; [ [ clang :: unavailable ( "main::widen is a generic function: call it from a .smith module" ) ] ] void main_widen ( void ) ;
int main(void) {
printf("%zu %zu %zu\n", sizeof(__extension__({extern int main_from_count__u8(void)__asm__("_main::from_count(__UINT8_TYPE__)");main_from_count__u8();})), sizeof(smith_call_failed),
sizeof(smith_call_failed));
printf("%zu %zu\n", sizeof(smith_call_failed), sizeof(smith_call_failed));
return 0;
}
extern int main_from_count__u8(void)__asm__("_main::from_count(__UINT8_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]int main_from_count__u8(void){return 7;}
