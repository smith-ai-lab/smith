__INT32_TYPE__ main(void) {
float three[4][3] = {};
float one[4] = {};struct smith_array_f32_n_2 {
float * data; __SIZE_TYPE__ count [1]; } rows = three;struct smith_array_f32_n_n {
float * data; __SIZE_TYPE__ count [2]; } flat = one;struct smith_array_f32_n_n {
float * data; __SIZE_TYPE__ count [2]; } fine = three;
return (__INT32_TYPE__)(countof(rows) + countof(flat) + countof(fine));
}
