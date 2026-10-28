struct smith_enum_result_i32__i32 {enum smith_enum_result_i32__i32_tag {smith_enum_result_i32__i32_tag_error, smith_enum_result_i32__i32_tag_ok } tag; union { __INT32_TYPE__ error; __INT32_TYPE__ ok; }; } __attribute__((annotate("smith::enum")));
static struct smith_enum_result_i32__i32 main_step(__INT32_TYPE__ n) {
return {.ok = n};
}struct smith_enum_option_i32 {enum smith_enum_option_i32_tag {smith_enum_option_i32_tag_none, smith_enum_option_i32_tag_some } tag; union { __INT32_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
static struct smith_enum_option_i32 main_pick(__INT32_TYPE__ n) {
return {.some = n};
}
static struct smith_enum_result_i32__i32 main_wrong_option(void) {
__INT32_TYPE__ a = try main_pick(1);
return {.ok = a};
}
static struct smith_enum_option_i32 main_wrong_result(void) {
__INT32_TYPE__ a = try main_step(1);
return {.some = a};
}
__INT32_TYPE__ main(void) {
return main_wrong_option() == .ok && main_wrong_result() == .some ? 0 : 1;
}
