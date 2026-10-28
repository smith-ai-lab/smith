static __INT32_TYPE__ main_forever(__INT32_TYPE__ n) {
while (n >= 0)
n += 1;
return n;
} [ [ clang :: unavailable ( "main::broken is a generic function: call it from a .smith module" ) ] ] void main_broken ( void ) ;
__INT32_TYPE__ main_call() __asm__("_main::call");
__INT32_TYPE__ main_call() {
return __extension__({extern __INT32_TYPE__ main_broken__I32(void)__asm__("_main::broken(__INT32_TYPE__)");main_broken__I32();});
}
extern __INT32_TYPE__ main_broken__I32(void)__asm__("_main::broken(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ main_broken__I32(void){return 0;}
