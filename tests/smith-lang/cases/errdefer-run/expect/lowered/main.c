#include <stdio.h>
struct smith_enum_result_i32__i32 {enum smith_enum_result_i32__i32_tag {smith_enum_result_i32__i32_tag_ok, smith_enum_result_i32__i32_tag_error } tag; union { __INT32_TYPE__ ok; __INT32_TYPE__ error; }; } __attribute__((annotate("smith::enum")));
static struct smith_enum_result_i32__i32 main_step(__INT32_TYPE__ n) {
if (n < 0) return {.error = n};
return {.ok = n};
}struct smith_enum_option_i32 {enum smith_enum_option_i32_tag {smith_enum_option_i32_tag_some, smith_enum_option_i32_tag_none } tag; union { __INT32_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
static struct smith_enum_option_i32 main_pick(__INT32_TYPE__ n) {
if (n < 0) return .none;
return {.some = n};
}
static struct smith_enum_result_i32__i32 main_chain(__INT32_TYPE__ a, __INT32_TYPE__ b) {
defer puts("  defer");
errdefer puts("  errdefer 1");
__INT32_TYPE__ x = try main_step(a);
errdefer printf("  errdefer 2 (x=%d)\n", x);
__INT32_TYPE__ y = try main_step(b);
return {.ok = x + y};
}
static struct smith_enum_result_i32__i32 main_by_hand(__INT32_TYPE__ n) {
errdefer puts("  hand errdefer");
if (n == 0) return {.error = 100};
struct smith_enum_result_i32__i32 r = main_step(n);
return r;
}
static struct smith_enum_option_i32 main_options(__INT32_TYPE__ a) {
errdefer puts("  option errdefer");
__INT32_TYPE__ v = try main_pick(a);
return {.some = v * 2};
}
static struct smith_enum_result_i32__i32 main_payload(__INT32_TYPE__ a) {
errdefer puts("  payload errdefer");
return {.ok = try main_step(a)};
}
static struct smith_enum_result_i32__i32 main_block_scope(__INT32_TYPE__ a) {
{
errdefer puts("  inner block errdefer");
__INT32_TYPE__ v = try main_step(a);
printf("  inner ok %d\n", v);
}
errdefer puts("  after the block");
return {.ok = 1};
}
static void main_show(const char *what, struct smith_enum_result_i32__i32 r) {
puts(what);
switch (r) {
case .ok: printf("ok %d\n", r.ok); break;
case .error: printf("error %d\n", r.error); break;
}
}
__INT32_TYPE__ main() {
main_show("chain ok", main_chain(1, 2));
main_show("chain, first fails", main_chain(-1, 2));
main_show("chain, second fails", main_chain(1, -2));
main_show("by hand, written error", main_by_hand(0));
main_show("by hand, ok", main_by_hand(5));
main_show("by hand, return r", main_by_hand(-5));
puts("option ok");
struct smith_enum_option_i32 good = main_options(3);
printf("%d\n", good == .some ? good.some : -1);
puts("option fails");
struct smith_enum_option_i32 bad = main_options(-3);
printf("%d\n", bad == .none);
main_show("payload ok", main_payload(6));
main_show("payload fails", main_payload(-6));
main_show("block ok", main_block_scope(2));
main_show("block fails", main_block_scope(-2));
return 0;
}
