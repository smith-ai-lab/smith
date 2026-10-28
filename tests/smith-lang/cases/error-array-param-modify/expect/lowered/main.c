static __INT32_TYPE__ main_move(__INT32_TYPE__ xs[static 2], __INT32_TYPE__ *other) {
xs++;
xs = other;
return xs[0];
}
__INT32_TYPE__ main(void) {
__INT32_TYPE__ xs[2] = {};
return main_move(xs, xs);
}
