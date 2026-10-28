struct pairs_Pair {
__INT32_TYPE__ left;
__INT32_TYPE__ right;
};
typedef struct pairs_Pair pairs_Pair;
struct smith_array_pairs_Pair_2 { pairs_Pair data [2]; } pairs_twin(pairs_Pair p) __asm__("_pairs::twin");
struct smith_array_pairs_Pair_2 { pairs_Pair data [2]; } pairs_twin(pairs_Pair p) {
return {p, {p.right, p.left}};
}
struct smith_array_i32_3 { __INT32_TYPE__ data [3]; } pairs_count_from(__INT32_TYPE__ start) __asm__("_pairs::count_from");
struct smith_array_i32_3 { __INT32_TYPE__ data [3]; } pairs_count_from(__INT32_TYPE__ start) {
__INT32_TYPE__ out[3] = {start, start + 1, start + 2};
return out;
}
struct smith_array_i32_3 pairs_counted(__INT32_TYPE__ start) __asm__("_pairs::counted");
struct smith_array_i32_3 pairs_counted(__INT32_TYPE__ start) {
return pairs_count_from(start);
}
struct smith_array_i32_n { __INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } pairs_tail(struct smith_array_i32_n xs) __asm__("_pairs::tail");
struct smith_array_i32_n { __INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } pairs_tail(struct smith_array_i32_n xs) {
return xs[1:];
}
