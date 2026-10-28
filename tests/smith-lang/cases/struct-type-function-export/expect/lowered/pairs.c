struct smith_struct_Pair_usize {__SIZE_TYPE__ first;__SIZE_TYPE__ second;
__SIZE_TYPE__ count; };
struct smith_struct_Pair_usize pairs_both(__SIZE_TYPE__ a, __SIZE_TYPE__ b) __asm__("_pairs::both");
struct smith_struct_Pair_usize pairs_both(__SIZE_TYPE__ a, __SIZE_TYPE__ b) {
struct smith_struct_Pair_usize pairs_both = {.first = a, .second = b, .count = 2};
return pairs_both;
}
