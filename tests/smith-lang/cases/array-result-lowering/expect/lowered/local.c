static struct smith_array_i32_2 {
__INT32_TYPE__ data [2]; } local_two(__INT32_TYPE__ x) {
return {x, x + 1};
}static struct smith_array_i32_2 local_again(__INT32_TYPE__ x) {
__INT32_TYPE__ keep[2] = local_two(x);
return keep;
}
static __INT32_TYPE__ local_first(__INT32_TYPE__ x) {
__INT32_TYPE__ pair[2] = local_again(x);
return pair[0];
}
