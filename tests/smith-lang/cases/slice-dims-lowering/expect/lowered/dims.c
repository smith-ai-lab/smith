struct dims_Image {struct smith_array_f32_n_n {
float * data; __SIZE_TYPE__ count [2]; } pixels;struct smith_array_u8_n_3 {
__UINT8_TYPE__ * data; __SIZE_TYPE__ count [1]; } rows;
};
typedef struct dims_Image dims_Image;
struct smith_array_f32_n_2 {float * data; __SIZE_TYPE__ count [1]; };
float dims_at(struct smith_array_f32_n_2 xs, __SIZE_TYPE__ i, __SIZE_TYPE__ j) __asm__("_dims::at");
float dims_at(struct smith_array_f32_n_2 xs, __SIZE_TYPE__ i, __SIZE_TYPE__ j) {
return xs[i][j];
}
struct smith_array_f32_n_2 dims_tail(struct smith_array_f32_n_2 xs) __asm__("_dims::tail");
struct smith_array_f32_n_2 dims_tail(struct smith_array_f32_n_2 xs) {
return xs[1:countof(xs)];
}
struct smith_array_i32_n_2_n {__INT32_TYPE__ * data; __SIZE_TYPE__ count [2]; };
__SIZE_TYPE__ dims_width(struct smith_array_i32_n_2_n cube, __SIZE_TYPE__ i) __asm__("_dims::width");
__SIZE_TYPE__ dims_width(struct smith_array_i32_n_2_n cube, __SIZE_TYPE__ i) {
return countof(cube[i]);
}
float dims_first(struct smith_array_f32_n_n xs) __asm__("_dims::first");
float dims_first(struct smith_array_f32_n_n xs) {struct smith_array_f32_n {
float * data; __SIZE_TYPE__ count [1]; } row = xs[0];
return row[0] + xs[0][1] + xs[1][countof(xs[0]) - 1];
}
