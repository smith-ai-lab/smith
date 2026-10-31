[ [ clang :: unavailable ( "main::pick is a generic function: call it from a .smith module" ) ] ] void main_pick ( void ) ;
__INT32_TYPE__ main(void) {
return __extension__({extern __INT32_TYPE__ main_pick__i32(__INT32_TYPE__ a)__asm__("_main::pick(__INT32_TYPE__)");main_pick__i32(1);});
}
extern __INT32_TYPE__ main_pick__i32(__INT32_TYPE__ a)__asm__("_main::pick(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ main_pick__i32(__INT32_TYPE__ a){__INT32_TYPE__(*f)(__INT32_TYPE__)=nullptr;return(__INT32_TYPE__)f(a);}
