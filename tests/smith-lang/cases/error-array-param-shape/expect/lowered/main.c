static __INT32_TYPE__ main_take(__INT32_TYPE__ xs[static 2]) {
return xs[0];
}
__INT32_TYPE__ main(void) {
__INT32_TYPE__ same[2] = {};
__INT32_TYPE__ longer[3] = {};
__INT32_TYPE__ *pointer = same;
return main_take(same) + main_take(longer) + main_take(pointer) + main_take(nullptr);
}
