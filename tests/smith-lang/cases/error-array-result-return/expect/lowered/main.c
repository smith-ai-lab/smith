static struct smith_array_i32_3 {
__INT32_TYPE__ data [3]; } main_three(__INT32_TYPE__ x) {
__INT32_TYPE__ two[2] = {x, x};
return two;
}
__INT32_TYPE__ main(void) {
__INT32_TYPE__ xs[3] = main_three(1);
return xs[0];
}
