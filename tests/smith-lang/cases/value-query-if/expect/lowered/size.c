static __SIZE_TYPE__ size_table_size(__SIZE_TYPE__ bytes) {
__SIZE_TYPE__ total = 0;
for (__SIZE_TYPE__ i = 0; i < bytes; i++)
total += i % 3 == 0 ? 2 : 1;
return total;
}
typedef struct {
__UINT8_TYPE__ bytes[80];
} size_Big; [ [ clang :: unavailable ( "size::choose is a generic function: call it from a .smith module" ) ] ] void size_choose ( void ) ; [ [ clang :: unavailable ( "size::mixed is a generic function: call it from a .smith module" ) ] ] void size_mixed ( void ) ;
__INT32_TYPE__ size_small() __asm__("_size::small");
__INT32_TYPE__ size_small() {
return __extension__({extern __INT32_TYPE__ size_choose__u8(void)__asm__("_size::choose(__UINT8_TYPE__)");size_choose__u8();});
}
__INT32_TYPE__ size_big() __asm__("_size::big");
__INT32_TYPE__ size_big() {
return __extension__({extern __INT32_TYPE__ size_choose__Big(void)__asm__("_size::choose(size_Big)");size_choose__Big();});
}
__INT32_TYPE__ size_all() __asm__("_size::all");
__INT32_TYPE__ size_all() {
return __extension__({extern __INT32_TYPE__ size_mixed__Big(void)__asm__("_size::mixed(size_Big)");size_mixed__Big();}) * 10000 +__extension__({extern __INT32_TYPE__ size_mixed__u32(void)__asm__("_size::mixed(__UINT32_TYPE__)");size_mixed__u32();}) * 100 +__extension__({extern __INT32_TYPE__ size_mixed__u8(void)__asm__("_size::mixed(__UINT8_TYPE__)");size_mixed__u8();});
}
extern __INT32_TYPE__ size_choose__u8(void)__asm__("_size::choose(__UINT8_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ size_choose__u8(void){}
extern __INT32_TYPE__ size_choose__Big(void)__asm__("_size::choose(size_Big)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ size_choose__Big(void){}
extern __INT32_TYPE__ size_mixed__Big(void)__asm__("_size::mixed(size_Big)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ size_mixed__Big(void){return 30;}
extern __INT32_TYPE__ size_mixed__u32(void)__asm__("_size::mixed(__UINT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ size_mixed__u32(void){return 30;}
extern __INT32_TYPE__ size_mixed__u8(void)__asm__("_size::mixed(__UINT8_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ size_mixed__u8(void){return 30;}
