typedef struct {struct smith_array_f32_n {
float * data; __SIZE_TYPE__ count [1]; } weights;struct smith_array_char_n {
char * data; __SIZE_TYPE__ count [1]; } name;
} slices_Layer;
typedef struct smith_array_i32_n { __INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } slices_Numbers;
__SIZE_TYPE__ slices_total(struct smith_array_i32_n xs, __SIZE_TYPE__ skip) __asm__("_slices::total");
__SIZE_TYPE__ slices_total(struct smith_array_i32_n xs, __SIZE_TYPE__ skip) {
__SIZE_TYPE__ sum = 0;
for (__SIZE_TYPE__ i = skip; i < countof(xs); i++) {
sum += (__SIZE_TYPE__)xs[i];
}
return sum;
}
struct smith_array_i32_n slices_middle(struct smith_array_i32_n xs) __asm__("_slices::middle");
struct smith_array_i32_n slices_middle(struct smith_array_i32_n xs) {
return xs[1:countof(xs) - 1];
}
struct smith_array_char_n slices_label(slices_Layer *layer) __asm__("_slices::label");
struct smith_array_char_n slices_label(slices_Layer *layer) {
return layer->name;
}
slices_Numbers slices_first_two(slices_Numbers all) __asm__("_slices::first_two");
slices_Numbers slices_first_two(slices_Numbers all) {
slices_Numbers two = all[:2];
return two;
}
