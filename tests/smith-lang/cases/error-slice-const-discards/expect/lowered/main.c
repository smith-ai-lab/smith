static __UINT8_TYPE__ main_first(struct smith_array_u8_n {__UINT8_TYPE__ * data; __SIZE_TYPE__ count [1]; } xs) {
return xs[0];
}
static struct smith_array_u8_n {__UINT8_TYPE__ * data; __SIZE_TYPE__ count [1]; } main_give(struct smith_array_const_u8_n {const __UINT8_TYPE__ * data; __SIZE_TYPE__ count [1]; } xs) {
return xs;
}
__INT32_TYPE__ main(void) {
__UINT8_TYPE__ raw[4] = {1, 2, 3, 4};struct smith_array_const_u8_n {
const __UINT8_TYPE__ * data; __SIZE_TYPE__ count [1]; } read = raw;struct smith_array_u8_n same = read;struct smith_array_u8_n other = raw;
other = read;struct smith_array_u8_n part = read[1:3];
return main_first(read);
}
