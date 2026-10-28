static struct smith_array_i32_2 {
__INT32_TYPE__ data [2]; } main_two(__INT32_TYPE__ x) {
return {x, x};
}
__INT32_TYPE__ main(void) {auto smith_w_three = main_two(1); static_assert (sizeof smith_w_three.data / sizeof smith_w_three.data [0] == 3, "the array has another size than the call's result");
__INT32_TYPE__ three[3]= {smith_w_three.data [0], smith_w_three.data [1], smith_w_three.data [2]};
return three[0];
}
