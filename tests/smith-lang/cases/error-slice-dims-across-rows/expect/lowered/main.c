__INT32_TYPE__ main(void) {
float raw[4][2] = {};struct smith_array_f32_n_2 {
float * data; __SIZE_TYPE__ count [1]; } xs = raw;struct smith_array_f32_n {
float * data; __SIZE_TYPE__ count [1]; } ok = xs[1][0:2];struct smith_array_f32_n_2 {
float * data; __SIZE_TYPE__ count [1]; } bad = xs[1:3][0:1];
return (__INT32_TYPE__)(ok[0] + (float)countof(bad));
}
