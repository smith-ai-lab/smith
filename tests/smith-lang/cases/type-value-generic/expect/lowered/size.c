[ [ clang :: unavailable ( "size::small is a function over `Type`: it exists only at compile time" ) ] ] void size_small ( void ) ;
typedef struct { __UINT8_TYPE__ bytes[80]; } size_Big; [ [ clang :: unavailable ( "size::choose is a generic function: call it from a .smith module" ) ] ] void size_choose ( void ) ;
__INT32_TYPE__ size_run() __asm__("_size::run");
__INT32_TYPE__ size_run() {
return __extension__({extern __INT32_TYPE__ size_choose__U8(void)__asm__("_size::choose(__UINT8_TYPE__)");size_choose__U8();}) * 100 +__extension__({extern __INT32_TYPE__ size_choose__Big(void)__asm__("_size::choose(size_Big)");size_choose__Big();}) * 10 +__extension__({extern __INT32_TYPE__ size_choose__F64(void)__asm__("_size::choose(double)");size_choose__F64();});
}
extern __INT32_TYPE__ size_choose__U8(void)__asm__("_size::choose(__UINT8_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ size_choose__U8(void){}
extern __INT32_TYPE__ size_choose__Big(void)__asm__("_size::choose(size_Big)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ size_choose__Big(void){}
extern __INT32_TYPE__ size_choose__F64(void)__asm__("_size::choose(double)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ size_choose__F64(void){}
