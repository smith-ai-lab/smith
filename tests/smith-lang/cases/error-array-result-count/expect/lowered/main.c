static struct smith_array_i32_2 {
__INT32_TYPE__ data [2]; } main_two(__INT32_TYPE__ x) {
return {x, x};
}
__INT32_TYPE__ main(void) {
__INT32_TYPE__ three[3] = main_two(1);
return three[0];
}
