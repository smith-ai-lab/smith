static __INT32_TYPE__ main_take(struct smith_array_i32_n {__INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } xs) {
return xs[0];
}
__INT32_TYPE__ main(void) {
__INT32_TYPE__ raw[6] = {1, 2, 3, 4, 5, 6};struct smith_array_i32_n {
__INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } all = raw;struct smith_array_i32_n {
__INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } every_other = all[0:6:2];
return main_take(every_other);
}
