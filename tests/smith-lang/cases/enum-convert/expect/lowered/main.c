#include <stdio.h>
struct smith_enum_result_f64__i32 {enum smith_enum_result_f64__i32_tag {smith_enum_result_f64__i32_tag_error, smith_enum_result_f64__i32_tag_ok } tag; union { __INT32_TYPE__ error; double ok; }; } __attribute__((annotate("smith::enum")));
typedef struct smith_enum_result_f64__i32 main_Quotient;struct smith_enum_option_i32 {enum smith_enum_option_i32_tag {smith_enum_option_i32_tag_none, smith_enum_option_i32_tag_some } tag; union { __INT32_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
typedef struct smith_enum_option_i32 main_Number;struct smith_enum_option_i64 {enum smith_enum_option_i64_tag {smith_enum_option_i64_tag_none, smith_enum_option_i64_tag_some } tag; union { __INT64_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
typedef struct smith_enum_option_i64 main_Wide;
struct main_Pair {
__INT32_TYPE__ left;
__INT32_TYPE__ right;
};
typedef struct main_Pair main_Pair;struct smith_enum_option_struct_main_Pair {enum smith_enum_option_struct_main_Pair_tag {smith_enum_option_struct_main_Pair_tag_none, smith_enum_option_struct_main_Pair_tag_some } tag; union { struct main_Pair some; }; } __attribute__((annotate("smith::enum")));
typedef struct smith_enum_option_struct_main_Pair main_Maybe;
static main_Quotient main_divide(double a, double b) {
if (b == 0) return {.error = 7};
return a / b;
}
static __INT32_TYPE__ main_describe(main_Number n, __INT32_TYPE__ other) {
return n == .some ? n.some : other;
}
static main_Number main_twice(main_Number n) {
if (n != .some) return .none;
return n.some * 2;
}
static main_Number main_absent(void) {
return .none;
}
__INT32_TYPE__ main(void) {
main_Number a = 5;
main_Number b;
b = 6;
main_Wide c = 7;
main_Number d = main_twice(8);
main_Number e = main_twice(main_absent());
main_Maybe f = (struct main_Pair){1, 2};
main_Quotient g = main_divide(9, 3);
main_Quotient h = main_divide(1, 0);
printf("%d %d %d %d\n", main_describe(a, -1), main_describe(b, -1), (__INT32_TYPE__)(c == .some ? c.some : -1), main_describe(d, -1));
printf("%d\n", main_describe(e, -1));
if (f == .some) printf("pair %d %d\n", f.some.left, f.some.right);
if (g == .ok) printf("quotient %.1f\n", g.ok);
if (h == .error) printf("error %d\n", h.error);
return 0;
}
