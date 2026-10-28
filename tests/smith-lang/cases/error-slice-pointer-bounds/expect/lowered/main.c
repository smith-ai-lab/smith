__INT32_TYPE__ main(void) {
__INT32_TYPE__ raw[3] = {1, 2, 3};
__INT32_TYPE__ *p = raw;struct smith_array_i32_n {
__INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } rest = p[1:];
return rest[0];
}
