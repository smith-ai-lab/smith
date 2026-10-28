__INT32_TYPE__ main(void) {
__UINT8_TYPE__ raw[4] = {1, 2, 3, 4};struct smith_array_const_u8_n {
const __UINT8_TYPE__ * data; __SIZE_TYPE__ count [1]; } west = raw;struct smith_array_const_u8_n {
__UINT8_TYPE__ const * data; __SIZE_TYPE__ count [1]; } east = raw;
west[0] = 9;
east[1] = 9;struct smith_array_const_u8_n {
const __UINT8_TYPE__ * data; __SIZE_TYPE__ count [1]; } cut = west[1:3];
cut[0] = 9;const struct smith_array_u8_n {
__UINT8_TYPE__ * data; __SIZE_TYPE__ count [1]; } fixed = raw;
fixed[0] = 9;
fixed = raw;
fixed = fixed[1:];
return 0;
}
