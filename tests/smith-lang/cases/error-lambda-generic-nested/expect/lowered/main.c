[ [ clang :: unavailable ( "main::twice is a generic function: call it from a .smith module" ) ] ] void main_twice ( void ) ;
__INT32_TYPE__ main(void) {
return __extension__({extern __INT32_TYPE__ main_twice__i32(__INT32_TYPE__ a)__asm__("_main::twice(__INT32_TYPE__)");main_twice__i32(1);});
}
extern __INT32_TYPE__ main_twice__i32(__INT32_TYPE__ a)__asm__("_main::twice(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ main_twice__i32(__INT32_TYPE__ a){__INT32_TYPE__(*f)(void)=nullptr;return f()+(__INT32_TYPE__)a;}
