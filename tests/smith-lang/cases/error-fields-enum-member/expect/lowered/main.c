typedef enum {
main_Red,
main_Green,
} main_Color; [ [ clang :: unavailable ( "main::first is a generic function: call it from a .smith module" ) ] ] void main_first ( void ) ;
__INT32_TYPE__ main(void) {
main_Color c = main_Red;
return __extension__({extern __INT32_TYPE__ main_first__Color(main_Color*x)__asm__("_main::first(main_Color)");main_first__Color(&c);});
}
extern __INT32_TYPE__ main_first__Color(main_Color*x)__asm__("_main::first(main_Color)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ main_first__Color(main_Color*x){return x->smith_call_failed;}
