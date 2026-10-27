#include <stdio.h>
#include <stdlib.h>

typedef struct {
__INT32_TYPE__ v;
} main_Node;
static __INT32_TYPE__ main_sum(struct smith_array_const_i32_n {const __INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } xs) {
__INT32_TYPE__ total = 0;
for (__SIZE_TYPE__ smith_rfor_i1 = 0; smith_rfor_i1 < countof ( xs); smith_rfor_i1 ++) {__INT32_TYPE__ x = xs [smith_rfor_i1];
total += x;
}
return total;
}
static __INT32_TYPE__ main_weighted(__INT32_TYPE__ xs[static 4]) {
__INT32_TYPE__ total = 0;
for (__SIZE_TYPE__ i = 0; i < countof ( xs); i ++) { __INT32_TYPE__ x = xs [ i];
total += x * (__INT32_TYPE__)i;
}
return total;
}
static __INT32_TYPE__ main_cut(struct smith_array_i32_n {__INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } xs) {
__INT32_TYPE__ total = 0;
for (__SIZE_TYPE__ i = 0; i < countof ( xs[2:5]); i ++) { __INT32_TYPE__ x = xs[2:5] [ i];
total += (__INT32_TYPE__)i * 100 + x;
}
return total;
}
static void main_double_all(struct smith_array_i32_n {__INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } xs) {
for (__SIZE_TYPE__ smith_rfor_i4 = 0; smith_rfor_i4 < countof ( xs); smith_rfor_i4 ++) {__INT32_TYPE__ *p = __builtin_choose_expr (__builtin_types_compatible_p (__INT32_TYPE__ *, __typeof__ ( xs [0])), xs [smith_rfor_i4], & xs [smith_rfor_i4]);
*p *= 2;
}
for (__SIZE_TYPE__ smith_rfor_i5 = 0; smith_rfor_i5 < countof ( xs); smith_rfor_i5 ++) {__INT32_TYPE__ x = xs [smith_rfor_i5];
x = 0;
}
}
static __INT32_TYPE__ main_nodes(struct smith_array_main_Node_ptr_n {main_Node * * data; __SIZE_TYPE__ count [1]; }ns) {
__INT32_TYPE__ total = 0;
for (__SIZE_TYPE__ smith_rfor_i6 = 0; smith_rfor_i6 < countof ( ns); smith_rfor_i6 ++) {main_Node *n = __builtin_choose_expr (__builtin_types_compatible_p (main_Node *, __typeof__ ( ns [0])), ns [smith_rfor_i6], & ns [smith_rfor_i6]);
total += n->v;
}
for (__SIZE_TYPE__ smith_rfor_i7 = 0; smith_rfor_i7 < countof ( ns); smith_rfor_i7 ++) {main_Node **pn = __builtin_choose_expr (__builtin_types_compatible_p (main_Node **, __typeof__ ( ns [0])), ns [smith_rfor_i7], & ns [smith_rfor_i7]);
*pn = nullptr;
}
return total;
}
static double main_mean(struct smith_array_const_f64_n {const double * data; __SIZE_TYPE__ count [1]; } xs) {
double total = 0;
for (__SIZE_TYPE__ smith_rfor_i8 = 0; smith_rfor_i8 < countof ( xs); smith_rfor_i8 ++) {auto x = xs [smith_rfor_i8];
total += x;
}
for (__SIZE_TYPE__ smith_rfor_i9 = 0; smith_rfor_i9 < countof ( xs); smith_rfor_i9 ++) {auto y = xs [smith_rfor_i9];
total += y;
}
return total / (double)(2 * countof(xs));
}
static __INT32_TYPE__ main_control(struct smith_array_const_i32_n {const __INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } xs) {
__INT32_TYPE__ total = 0;
for (__SIZE_TYPE__ smith_rfor_i10 = 0; smith_rfor_i10 < countof ( xs); smith_rfor_i10 ++) {__INT32_TYPE__ x = xs [smith_rfor_i10];
defer total += 1000;
if (x == 2) continue;
if (x == 4) break;
total += x;
}
return total;
}struct smith_enum_result_i32__u8 {enum smith_enum_result_i32__u8_tag {smith_enum_result_i32__u8_tag_ok, smith_enum_result_i32__u8_tag_error } tag; union { __INT32_TYPE__ ok; __UINT8_TYPE__ error; }; } __attribute__((annotate("smith::enum")));
static struct smith_enum_result_i32__u8 main_checked(struct smith_array_const_i32_n {const __INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } xs) {
__INT32_TYPE__ total = 0;
for (__SIZE_TYPE__ smith_rfor_i11 = 0; smith_rfor_i11 < countof ( xs); smith_rfor_i11 ++) {__INT32_TYPE__ x = xs [smith_rfor_i11];
if (x < 0) return {.error = 1};
total += x;
}
return {.ok = total};
}
static __INT32_TYPE__ main_pairs(struct smith_array_const_i32_n {const __INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } a,struct smith_array_const_i32_n { const __INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } b) {
__INT32_TYPE__ total = 0;
for (__SIZE_TYPE__ i = 0; i < countof ( a); i ++) { __INT32_TYPE__ x = a [ i];
for (__SIZE_TYPE__ j = 0; j < countof ( b); j ++) { __INT32_TYPE__ y = b [ j];
total += x * y * (__INT32_TYPE__)(i + j + 1);
}
}
return total;
}
void smith_failure(__UINT8_TYPE__ code);
void smith_failure(__UINT8_TYPE__ code) {
exit(code);
}
__INT32_TYPE__ main(void) {
__INT32_TYPE__ raw[4] = {1, 2, 3, 4};
__INT32_TYPE__ more[6] = {10, 20, 30, 40, 50, 60};struct smith_array_i32_n {
__INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } all = raw;
printf("%d %d %d\n", main_sum(all), main_weighted(raw), main_cut(more));
main_double_all(all);
printf("%d %d %d %d\n", raw[0], raw[1], raw[2], raw[3]);
main_Node a = {5};
main_Node b = {6};
main_Node *ptrs[2] = {&a, &b};struct smith_array_main_Node_ptr_n {
main_Node * * data; __SIZE_TYPE__ count [1]; }pointers = ptrs;
__INT32_TYPE__ node_total = main_nodes(pointers);
printf("%d %d\n", node_total, ptrs[0] == nullptr);
double values[3] = {1.0, 2.0, 6.0};struct smith_array_f64_n {
double * data; __SIZE_TYPE__ count [1]; } view = values;
__INT32_TYPE__ five[5] = {1, 2, 3, 4, 5};
printf("%g %d\n", main_mean(view), main_control(five));
__INT32_TYPE__ odd[4] = {1, 2, 3, 4};
struct smith_enum_result_i32__u8 good = main_checked(odd);
__INT32_TYPE__ negative[2] = {1, -1};
struct smith_enum_result_i32__u8 bad = main_checked(negative);
printf("%d %d %d\n", good == .ok, bad == .error, main_pairs(all, odd));
return 0;
}
