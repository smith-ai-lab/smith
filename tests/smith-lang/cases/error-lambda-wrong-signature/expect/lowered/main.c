[ [ clang :: unavailable ( "main::apply is a generic function: call it from a .smith module" ) ] ] void main_apply ( void ) ;
__INT32_TYPE__ main(void) {
return __extension__({extern __INT32_TYPE__ main_apply__lambda(__INT32_TYPE__ x)__asm__("_main::apply(lambda#b3b536d6fd1d9c77)@src/main.smith");main_apply__lambda(4);});
}
static __INT32_TYPE__ main_apply__lambda__arg0(double a){return 0;}_Static_assert(__builtin_types_compatible_p(__typeof__(main_apply__lambda__arg0),__INT32_TYPE__(__INT32_TYPE__)),"smith: the argument for `f` of `apply` does not have the type of `lambda(i32 (i32))`");extern __INT32_TYPE__ main_apply__lambda(__INT32_TYPE__ x)__asm__("_main::apply(lambda#b3b536d6fd1d9c77)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ main_apply__lambda(__INT32_TYPE__ x){return main_apply__lambda__arg0(x);}
