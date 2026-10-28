__INT32_TYPE__ main(void) {
__INT32_TYPE__ raw[3] = {1, 2, 3};struct smith_array_i32_n {
__INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } all = raw;
__INT32_TYPE__ last = all[-1];
return last;
}
