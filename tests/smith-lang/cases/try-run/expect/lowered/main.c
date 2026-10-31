#include <stdio.h>
struct smith_enum_result_i32__i32 {enum smith_enum_result_i32__i32_tag {smith_enum_result_i32__i32_tag_ok, smith_enum_result_i32__i32_tag_error } tag; union { __INT32_TYPE__ ok; __INT32_TYPE__ error; }; } __attribute__((annotate("std::enum")));
static struct smith_enum_result_i32__i32 main_step(__INT32_TYPE__ n) {
if (n < 0) return {.error = n};
return {.ok = n};
}struct smith_enum_option_i32 {enum smith_enum_option_i32_tag {smith_enum_option_i32_tag_some, smith_enum_option_i32_tag_none } tag; union { __INT32_TYPE__ some; }; } __attribute__((annotate("std::enum")));
static struct smith_enum_option_i32 main_pick(__INT32_TYPE__ n) {
if (n < 0) return .none;
return {.some = n};
}
static struct smith_enum_result_i32__i32 main_declared(__INT32_TYPE__ a, __INT32_TYPE__ b) {
defer puts("  declared leaves");
__INT32_TYPE__ x = try main_step(a);
defer puts("  second defer");
__INT32_TYPE__ y = try main_step(b);
return {.ok = x + y};
}
static struct smith_enum_result_i32__i32 main_assigned(__INT32_TYPE__ a) {
__INT32_TYPE__ x = 0;
x = try main_step(a);
try main_step(a - 1);
return {.ok = x};
}
static struct smith_enum_result_i32__i32 main_returned(__INT32_TYPE__ a) {
return {.ok = try main_step(a)};
}
static struct smith_enum_option_i32 main_options(__INT32_TYPE__ a) {
__INT32_TYPE__ v = try main_pick(a);
return {.some = v * 2};
}
static struct smith_enum_option_i32 main_payload_option(__INT32_TYPE__ a) {
return {.some = try main_pick(a)};
}
static void main_show(struct smith_enum_result_i32__i32 r) {
switch (r) {
case .ok: printf("ok %d\n", r.ok); break;
case .error: printf("error %d\n", r.error); break;
}
}
__INT32_TYPE__ main(void) {
main_show(main_declared(1, 2));
main_show(main_declared(-1, 2));
main_show(main_declared(1, -2));
main_show(main_assigned(5));
main_show(main_assigned(0));
main_show(main_returned(4));
main_show(main_returned(-4));
struct smith_enum_option_i32 good = main_options(3);
struct smith_enum_option_i32 bad = main_options(-3);
printf("%d %d\n", good == .some ? good.some : -1, bad == .none);
struct smith_enum_option_i32 viaPayload = main_payload_option(7);
printf("%d\n", viaPayload == .some ? viaPayload.some : -1);
return 0;
}
