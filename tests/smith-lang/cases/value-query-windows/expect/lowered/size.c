static __SIZE_TYPE__ size_long_size() {
return sizeof(long);
} [ [ clang :: unavailable ( "size::long_is_four is a generic function: call it from a .smith module" ) ] ] void size_long_is_four ( void ) ;
__INT32_TYPE__ size_use() __asm__("_size::use");
__INT32_TYPE__ size_use() {
return __extension__({extern __INT32_TYPE__ size_long_is_four__I32(void)__asm__("_size::long_is_four(__INT32_TYPE__)");size_long_is_four__I32();});
}
extern __INT32_TYPE__ size_long_is_four__I32(void)__asm__("_size::long_is_four(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ size_long_is_four__I32(void){return 8;}
