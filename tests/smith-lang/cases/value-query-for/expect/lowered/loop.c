struct loop_Config {
__SIZE_TYPE__ count;
__INT32_TYPE__ step;
};
typedef struct loop_Config loop_Config;
static constexpr struct loop_Config loop_config = {.count = 4, .step = 3}; [ [ clang :: unavailable ( "loop::total is a generic function: call it from a .smith module" ) ] ] void loop_total ( void ) ;
__INT32_TYPE__ loop_bytes() __asm__("_loop::bytes");
__INT32_TYPE__ loop_bytes() {
return __extension__({extern __INT32_TYPE__ loop_total__U8(void)__asm__("_loop::total(__UINT8_TYPE__)");loop_total__U8();});
}
__INT32_TYPE__ loop_words() __asm__("_loop::words");
__INT32_TYPE__ loop_words() {
return __extension__({extern __INT32_TYPE__ loop_total__U64(void)__asm__("_loop::total(__UINT64_TYPE__)");loop_total__U64();});
}
extern __INT32_TYPE__ loop_total__U8(void)__asm__("_loop::total(__UINT8_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ loop_total__U8(void){__INT32_TYPE__ sum=0;return sum;}
extern __INT32_TYPE__ loop_total__U64(void)__asm__("_loop::total(__UINT64_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ loop_total__U64(void){__INT32_TYPE__ sum=0;return sum;}
