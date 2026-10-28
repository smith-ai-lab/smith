struct smith_enum_result_i32__i32 {enum smith_enum_result_i32__i32_tag {smith_enum_result_i32__i32_tag_ok, smith_enum_result_i32__i32_tag_error } tag; union { __INT32_TYPE__ ok; __INT32_TYPE__ error; }; } __attribute__((annotate("std::enum")));
static struct smith_enum_result_i32__i32 main_step(__INT32_TYPE__ n) {
return {.ok = n};
}struct smith_enum_result_i32__i64 {enum smith_enum_result_i32__i64_tag {smith_enum_result_i32__i64_tag_ok, smith_enum_result_i32__i64_tag_error } tag; union { __INT32_TYPE__ ok; __INT64_TYPE__ error; }; } __attribute__((annotate("std::enum")));
static struct smith_enum_result_i32__i64 main_other_error(void) {
__INT32_TYPE__ a = try main_step(1);
return {.ok = a};
}
__INT32_TYPE__ main(void) {
return main_other_error() == .ok ? 0 : 1;
}
