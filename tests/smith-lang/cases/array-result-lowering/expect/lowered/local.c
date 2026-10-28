static struct smith_array_i32_2 {
__INT32_TYPE__ data [2]; } local_two(__INT32_TYPE__ x) {
return {x, x + 1};
}static struct smith_array_i32_2 local_again(__INT32_TYPE__ x) {auto smith_w_keep = local_two(x); static_assert (sizeof smith_w_keep.data / sizeof smith_w_keep.data [0] == 2, "the array has another size than the call's result");
__INT32_TYPE__ keep[2]= {smith_w_keep.data [0], smith_w_keep.data [1]};
return keep;
}
static __INT32_TYPE__ local_first(__INT32_TYPE__ x) {auto smith_w_pair = local_again(x); static_assert (sizeof smith_w_pair.data / sizeof smith_w_pair.data [0] == 2, "the array has another size than the call's result");
__INT32_TYPE__ pair[2]= {smith_w_pair.data [0], smith_w_pair.data [1]};
return pair[0];
}
