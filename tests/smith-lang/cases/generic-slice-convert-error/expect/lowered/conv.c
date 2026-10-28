static __SIZE_TYPE__ conv_plain_count(struct smith_array_u8_n {__UINT8_TYPE__ * data; __SIZE_TYPE__ count [1]; } xs) {
return countof(xs);
}
struct smith_array_const_u8_n {const __UINT8_TYPE__ * data; __SIZE_TYPE__ count [1]; };
__SIZE_TYPE__ conv_ordinary(struct smith_array_const_u8_n xs) __asm__("_conv::ordinary");
__SIZE_TYPE__ conv_ordinary(struct smith_array_const_u8_n xs) {
return conv_plain_count(xs);
} [ [ clang :: unavailable ( "conv::generic is a generic function: call it from a .smith module" ) ] ] void conv_generic ( void ) ;
