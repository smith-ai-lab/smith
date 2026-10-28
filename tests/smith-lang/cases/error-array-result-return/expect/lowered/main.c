static struct smith_array_i32_3 {
__INT32_TYPE__ data [3]; } main_three(__INT32_TYPE__ x) {
__INT32_TYPE__ two[2] = {x, x};
return two;
}
__INT32_TYPE__ main(void) {auto smith_w_xs = main_three(1); static_assert (sizeof smith_w_xs.data / sizeof smith_w_xs.data [0] == 3, "the array has another size than the call's result");
__INT32_TYPE__ xs[3]= {smith_w_xs.data [0], smith_w_xs.data [1], smith_w_xs.data [2]};
return xs[0];
}
