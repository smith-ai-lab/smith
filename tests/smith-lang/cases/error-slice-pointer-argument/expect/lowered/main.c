static __INT32_TYPE__ main_take(struct smith_array_i32_n {__INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } xs) {
return xs[0];
}
__INT32_TYPE__ main(void) {
__INT32_TYPE__ raw[3] = {1, 2, 3};
__INT32_TYPE__ *p = raw;
return main_take(p);
}
