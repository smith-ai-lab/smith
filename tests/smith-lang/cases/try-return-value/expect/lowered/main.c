#include <stdio.h>
struct smith_enum_result_i32__u8 {enum smith_enum_result_i32__u8_tag {smith_enum_result_i32__u8_tag_ok, smith_enum_result_i32__u8_tag_error } tag; union { __INT32_TYPE__ ok; __UINT8_TYPE__ error; }; } __attribute__((annotate("std::enum")));
typedef struct smith_enum_result_i32__u8 main_Step;struct smith_enum_option_i32 {enum smith_enum_option_i32_tag {smith_enum_option_i32_tag_some, smith_enum_option_i32_tag_none } tag; union { __INT32_TYPE__ some; }; } __attribute__((annotate("std::enum")));
typedef struct smith_enum_option_i32 main_Found;
static main_Step main_step(__INT32_TYPE__ n) {
if (n < 0) return {.error = 3};
return n * 2;
}
static main_Step main_forward(__INT32_TYPE__ n) {
return try main_step(n);
}
static main_Found main_first(__INT32_TYPE__ n) {
if (n == 0) return .none;
return n;
}
static main_Found main_again(__INT32_TYPE__ n) {
defer puts("again leaves");
errdefer puts("again failed");
return try main_first(n);
}
__INT32_TYPE__ main(void) {
main_Step a = main_forward(4);
main_Step b = main_forward(-1);
main_Found c = main_again(5);
main_Found d = main_again(0);
if (a == .ok) printf("ok %d\n", a.ok);
if (b == .error) printf("error %d\n", b.error);
if (c == .some) printf("some %d\n", c.some);
if (d == .none) puts("none");
return 0;
}
