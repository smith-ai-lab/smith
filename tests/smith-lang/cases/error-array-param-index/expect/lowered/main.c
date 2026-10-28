static __INT32_TYPE__ main_vector(__INT32_TYPE__ xs[static 2]) {
return xs[0] + xs[2] + xs[-1];
}
static float main_matrix(float m[static 3][2]) {
return m[2][1] + m[3][0] + m[0][2];
}
__INT32_TYPE__ main(void) {
__INT32_TYPE__ xs[2] = {};
float m[3][2] = {};
return main_vector(xs) + (__INT32_TYPE__)main_matrix(m);
}
