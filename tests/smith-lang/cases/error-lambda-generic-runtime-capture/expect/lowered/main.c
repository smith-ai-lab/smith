typedef __INT32_TYPE__ (*main_Call)(void); [ [ clang :: unavailable ( "main::scaled is a generic function: call it from a .smith module" ) ] ] void main_scaled ( void ) ;
__INT32_TYPE__ main(void) {
main_Call call =__extension__({extern main_Call main_scaled__i32(__INT32_TYPE__ factor)__asm__("_main::scaled(__INT32_TYPE__)");main_scaled__i32(3);});
return call();
}
extern main_Call main_scaled__i32(__INT32_TYPE__ factor)__asm__("_main::scaled(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]main_Call main_scaled__i32(__INT32_TYPE__ factor){return nullptr;}
