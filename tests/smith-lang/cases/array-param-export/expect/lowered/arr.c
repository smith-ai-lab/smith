__INT32_TYPE__ arr_pick(__INT32_TYPE__ xs[static 4], __SIZE_TYPE__ i) __asm__("_arr::pick");
__INT32_TYPE__ arr_pick(__INT32_TYPE__ xs[static 4], __SIZE_TYPE__ i) {
return xs[i];
}
float arr_corner(float m[static 3][2]) __asm__("_arr::corner");
float arr_corner(float m[static 3][2]) {
return m[2][1];
}
